#include "http.hpp"

#include "log.hpp"
#include "paths.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

#ifdef __SWITCH__
#include <switch/runtime/devices/fs_dev.h>
#endif

// Embedded from runtime/assets/certs/cacert.pem by CMake.
extern "C" const unsigned char cacert_pem[];
extern "C" const unsigned char cacert_pem_end[];

namespace launcher::http {
namespace {
bool g_init{};
std::string g_ca_path;
std::mutex g_init_mutex;

size_t WriteFile(void* data, size_t size, size_t count, void* userdata) {
    FILE* file = static_cast<FILE*>(userdata);
    return std::fwrite(data, size, count, file);
}

size_t WriteStream(void* data, size_t size, size_t count, void* userdata) {
    auto* callback = static_cast<const DataCallback*>(userdata);
    const size_t bytes = size * count;
    if (callback != nullptr && *callback != nullptr) {
        return (*callback)(static_cast<const u8*>(data), bytes) ? bytes : 0;
    }
    return bytes;
}

struct MemoryContext {
    std::vector<u8>* out{};
    size_t max_size{};
    bool too_large{};
};

size_t WriteMemory(void* data, size_t size, size_t count, void* userdata) {
    auto* ctx = static_cast<MemoryContext*>(userdata);
    const size_t bytes = size * count;
    if (ctx->out->size() + bytes > ctx->max_size) {
        ctx->too_large = true;
        return 0;
    }
    const auto* src = static_cast<const u8*>(data);
    ctx->out->insert(ctx->out->end(), src, src + bytes);
    return bytes;
}

struct ProgressCtx {
    const Progress* progress{};
    const Cancel* cancel{};
    bool aborted{};
    curl_off_t downloaded{};
};

int SetReceiveBuffer(void*, curl_socket_t fd, curlsocktype) {
    const int size = 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    return CURL_SOCKOPT_OK;
}

int OnProgress(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ProgressCtx*>(userdata);
    if (ctx->cancel != nullptr && *ctx->cancel && (*ctx->cancel)()) {
        ctx->aborted = true;
        return 1;
    }
    ctx->downloaded = dlnow;
    if (ctx->progress != nullptr && *ctx->progress != nullptr) {
        if (!(*ctx->progress)(dlnow, dltotal)) {
            ctx->aborted = true;
            return 1;
        }
    }
    return 0;
}

bool WriteCertificateBundle(const std::string& path) {
    const auto expected = static_cast<size_t>(cacert_pem_end - cacert_pem);
    std::error_code ec;
    const auto actual = std::filesystem::file_size(path, ec);
    if (!ec && actual == expected) {
        return true;
    }

    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        log_write("[http] cannot write %s\n", path.c_str());
        return false;
    }
    const size_t written = std::fwrite(cacert_pem, 1, expected, file);
    const bool ok = written == expected && std::fclose(file) == 0;
    if (!ok) {
        std::remove(path.c_str());
    }
    return ok;
}

CURL* MakeHandle(const std::string& url, ProgressCtx* ctx) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return nullptr;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "nsmbw-nx-launcher");
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L * 1024L);
    curl_easy_setopt(curl, CURLOPT_SOCKOPTFUNCTION, SetReceiveBuffer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, OnProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, ctx);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (!g_ca_path.empty()) {
        curl_easy_setopt(curl, CURLOPT_CAINFO, g_ca_path.c_str());
    }
    return curl;
}

constexpr s64 kParallelThreshold = 32 * 1024 * 1024;
constexpr int kParallelConnections = 3;
constexpr size_t kRangeStackSize = 512 * 1024;

struct RangeDownload {
    const std::string* url{};
    const std::string* path{};
    curl_slist* resolve{};
    s64 start{};
    s64 end{};
    s64 offset{};
#ifdef __SWITCH__
    FsFile* file{};
#else
    int fd{-1};
#endif
    std::mutex* file_mutex{};
    std::atomic<bool>* abort{};
    std::atomic<s64>* downloaded{};
    std::atomic<int>* done{};
    bool failed{};
    CURLcode curl_result{CURLE_OK};
    long response{};
    pthread_t thread{};
};

struct ResolvedUrl {
    std::string url;
    std::string resolve;
};

bool Preallocate(const std::string& path, s64 size) {
#ifdef __SWITCH__
    Result rc = fsdevCreateFile(path.c_str(), size, 0);
    if (R_FAILED(rc)) {
        return false;
    }
    FsFileSystem* device = nullptr;
    char fs_path[FS_MAX_PATH]{};
    if (fsdevTranslatePath(path.c_str(), &device, fs_path) != 0 || device == nullptr) {
        return false;
    }
    FsFile file{};
    rc = fsFsOpenFile(device, fs_path, FsOpenMode_Write, &file);
    if (R_FAILED(rc)) {
        return false;
    }
    rc = fsFileSetSize(&file, size);
    fsFileClose(&file);
    return R_SUCCEEDED(rc);
#else
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        return false;
    }
    const bool ok = ftruncate(fd, static_cast<off_t>(size)) == 0;
    close(fd);
    return ok;
#endif
}

bool ResolveFinalUrl(const std::string& url, ResolvedUrl& out) {
    CURL* curl = MakeHandle(url, nullptr);
    if (curl == nullptr) {
        return false;
    }
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    const CURLcode rc = curl_easy_perform(curl);
    long code{};
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    char* effective = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective);
    if (rc == CURLE_OK && code < 400 && effective != nullptr) {
        out.url = effective;
    }
    curl_easy_cleanup(curl);
    if (out.url.empty()) {
        return false;
    }

    CURLU* parts = curl_url();
    if (parts == nullptr) {
        return false;
    }
    char* host = nullptr;
    char* port = nullptr;
    bool ok = false;
    if (curl_url_set(parts, CURLUPART_URL, out.url.c_str(), 0) == CURLUE_OK &&
        curl_url_get(parts, CURLUPART_HOST, &host, 0) == CURLUE_OK &&
        curl_url_get(parts, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT) == CURLUE_OK) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(host, port, &hints, &result) == 0 && result != nullptr) {
            char ip[INET_ADDRSTRLEN]{};
            auto* address = reinterpret_cast<sockaddr_in*>(result->ai_addr);
            if (inet_ntop(AF_INET, &address->sin_addr, ip, sizeof(ip)) != nullptr) {
                out.resolve = std::string(host) + ":" + port + ":" + ip;
                ok = true;
            }
            freeaddrinfo(result);
        }
    }
    curl_free(host);
    curl_free(port);
    curl_url_cleanup(parts);
    return ok;
}

size_t WriteRange(void* data, size_t size, size_t count, void* userdata) {
    auto* state = static_cast<RangeDownload*>(userdata);
    if (state->abort->load(std::memory_order_relaxed)) {
        return 0;
    }

    const size_t bytes = size * count;
    const s64 left = state->end - state->offset + 1;
    if (left <= 0) {
        return 0;
    }
    const size_t write_bytes = static_cast<size_t>(std::min<s64>(static_cast<s64>(bytes), left));
#ifdef __SWITCH__
    std::lock_guard lock(*state->file_mutex);
    const Result write_rc =
        fsFileWrite(state->file, state->offset, data, write_bytes, FsWriteOption_None);
    if (R_FAILED(write_rc)) {
        log_write("[http] range write failed 0x%08X at %lld size %zu\n",
                  static_cast<unsigned>(write_rc), static_cast<long long>(state->offset),
                  write_bytes);
        return 0;
    }
#else
    if (pwrite(state->fd, data, write_bytes, static_cast<off_t>(state->offset)) !=
        static_cast<ssize_t>(write_bytes)) {
        return 0;
    }
#endif
    state->offset += static_cast<s64>(write_bytes);
    state->downloaded->fetch_add(static_cast<s64>(write_bytes), std::memory_order_relaxed);

    return write_bytes == bytes ? bytes : 0;
}

void* RangeEntry(void* arg) {
    auto* state = static_cast<RangeDownload*>(arg);
#ifdef __SWITCH__
    // Keep range transfers on the worker cores.
    svcSetThreadCoreMask(threadGetCurHandle(), -1, (1u << 1) | (1u << 2));
#endif
    state->offset = state->start;

    CURL* curl = MakeHandle(*state->url, nullptr);
    if (curl == nullptr) {
        log_write("[http] range %lld-%lld: curl init failed\n",
                  static_cast<long long>(state->start), static_cast<long long>(state->end));
        state->failed = true;
        state->abort->store(true);
        state->done->fetch_add(1);
        return nullptr;
    }

    char range[64];
    std::snprintf(range, sizeof(range), "%lld-%lld", static_cast<long long>(state->start),
                  static_cast<long long>(state->end));
    curl_easy_setopt(curl, CURLOPT_RANGE, range);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L * 256L);
    if (state->resolve != nullptr) {
        curl_easy_setopt(curl, CURLOPT_RESOLVE, state->resolve);
    }
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteRange);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, state);

    state->curl_result = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &state->response);
    curl_easy_cleanup(curl);

    const s64 expected = state->end - state->start + 1;
    if (state->curl_result != CURLE_OK || state->response != 206 ||
        state->offset != state->start + expected) {
        log_write("[http] range %lld-%lld failed: %s code %ld offset %lld/%lld\n",
                  static_cast<long long>(state->start), static_cast<long long>(state->end),
                  curl_easy_strerror(state->curl_result), state->response,
                  static_cast<long long>(state->offset - state->start),
                  static_cast<long long>(expected));
        state->failed = true;
        state->abort->store(true);
    }
    state->done->fetch_add(1);
    return nullptr;
}
} // namespace

bool Init() {
    std::lock_guard lock(g_init_mutex);
    if (g_init) {
        return true;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        log_write("[http] curl_global_init failed\n");
        return false;
    }

    g_ca_path = (std::filesystem::path(RootDirectory()) / "cacert.pem").string();
    if (!WriteCertificateBundle(g_ca_path)) {
        log_write("[http] no CA bundle, network installs are disabled\n");
        g_ca_path.clear();
        curl_global_cleanup();
        return false;
    }

    g_init = true;
    return true;
}

void Exit() {
    std::lock_guard lock(g_init_mutex);
    if (g_init) {
        curl_global_cleanup();
        g_init = false;
    }
    g_ca_path.clear();
}

namespace {
Result DownloadSingle(const std::string& url, const std::string& path, const Progress& progress) {
    if (!Init()) {
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }
    const auto start = std::chrono::steady_clock::now();

    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        log_write("[http] cannot open %s\n", path.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    ProgressCtx ctx{&progress, nullptr, false, 0};
    CURL* curl = MakeHandle(url, &ctx);
    if (curl == nullptr) {
        std::fclose(file);
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

    const CURLcode rc = curl_easy_perform(curl);
    long code{};
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    const bool ok = std::fflush(file) == 0;
    std::fclose(file);

    if (rc != CURLE_OK || !ok || code >= 400) {
        log_write("[http] %s failed: %s code %ld\n", url.c_str(), curl_easy_strerror(rc), code);
        std::remove(path.c_str());
        if (ctx.aborted) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    if (ctx.downloaded > 0) {
        const auto seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - start).count();
        if (seconds > 0) {
            log_write("[http] %s %lld bytes in %.1fs (%.0f KB/s)\n", url.c_str(),
                      static_cast<long long>(ctx.downloaded), seconds,
                      static_cast<double>(ctx.downloaded) / seconds / 1024.0);
        }
    }

    return 0;
}

Result DownloadRanges(const std::string& url, const std::string& path, s64 expected_size,
                      int connections, const Progress& progress, bool& retry_single,
                      bool& cancelled) {
    retry_single = false;
    cancelled = false;
    const auto start_time = std::chrono::steady_clock::now();

    ResolvedUrl resolved;
    if (!ResolveFinalUrl(url, resolved)) {
        log_write("[http] could not resolve the download host\n");
        retry_single = true;
        return 0;
    }
    curl_slist* resolve_list = curl_slist_append(nullptr, resolved.resolve.c_str());
    if (resolve_list == nullptr) {
        retry_single = true;
        return 0;
    }

    if (!Preallocate(path, expected_size)) {
        std::remove(path.c_str());
        curl_slist_free_all(resolve_list);
        retry_single = true;
        return 0;
    }

#ifdef __SWITCH__
    FsFile file{};
    FsFileSystem* device = nullptr;
    char fs_path[FS_MAX_PATH]{};
    if (fsdevTranslatePath(path.c_str(), &device, fs_path) != 0 || device == nullptr ||
        R_FAILED(fsFsOpenFile(device, fs_path, FsOpenMode_Write, &file))) {
        std::remove(path.c_str());
        curl_slist_free_all(resolve_list);
        retry_single = true;
        return 0;
    }
#else
    const int fd = open(path.c_str(), O_WRONLY);
    if (fd < 0) {
        std::remove(path.c_str());
        curl_slist_free_all(resolve_list);
        retry_single = true;
        return 0;
    }
#endif
    std::mutex file_mutex;

    std::atomic<bool> abort{false};
    std::atomic<s64> downloaded{0};
    std::atomic<int> done{0};
    std::vector<RangeDownload> ranges(static_cast<size_t>(connections));
    const s64 chunk = (expected_size + connections - 1) / connections;
    int started = 0;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kRangeStackSize);

    for (int i = 0; i < connections; ++i) {
        const s64 range_start = static_cast<s64>(i) * chunk;
        if (range_start >= expected_size) {
            break;
        }
        const s64 range_end = std::min<s64>(expected_size, range_start + chunk) - 1;
        auto& range = ranges[static_cast<size_t>(i)];
        range.url = &resolved.url;
        range.resolve = resolve_list;
        range.start = range_start;
        range.end = range_end;
#ifdef __SWITCH__
        range.file = &file;
#else
        range.fd = fd;
#endif
        range.file_mutex = &file_mutex;
        range.abort = &abort;
        range.downloaded = &downloaded;
        range.done = &done;
        if (pthread_create(&range.thread, &attr, RangeEntry, &range) != 0) {
            log_write("[http] range thread create failed\n");
            abort.store(true);
            retry_single = true;
            break;
        }
        ++started;
    }
    pthread_attr_destroy(&attr);

    if (retry_single) {
        abort.store(true);
        for (int i = 0; i < started; ++i) {
            pthread_join(ranges[static_cast<size_t>(i)].thread, nullptr);
        }
        curl_slist_free_all(resolve_list);
#ifdef __SWITCH__
        fsFileClose(&file);
#else
        close(fd);
#endif
        std::remove(path.c_str());
        return 0;
    }

    bool progress_ok = true;
    while (done.load() < started) {
        if (progress) {
            if (!progress(downloaded.load(), expected_size)) {
                progress_ok = false;
                abort.store(true);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (int i = 0; i < started; ++i) {
        pthread_join(ranges[static_cast<size_t>(i)].thread, nullptr);
    }

    curl_slist_free_all(resolve_list);
#ifdef __SWITCH__
    fsFileClose(&file);
#else
    close(fd);
#endif

    if (!progress_ok) {
        std::remove(path.c_str());
        cancelled = true;
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    bool failed = false;
    for (int i = 0; i < started; ++i) {
        if (ranges[static_cast<size_t>(i)].failed) {
            failed = true;
        }
    }
    if (failed || downloaded.load() != expected_size) {
        log_write("[http] parallel download incomplete (%lld/%lld bytes)\n",
                  static_cast<long long>(downloaded.load()), static_cast<long long>(expected_size));
        std::remove(path.c_str());
        retry_single = true;
        return 0;
    }

    const auto seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
    if (seconds > 0) {
        log_write("[http] %s %lld bytes in %.1fs (%.0f KB/s, %d connections)\n", url.c_str(),
                  static_cast<long long>(downloaded.load()), seconds,
                  static_cast<double>(downloaded.load()) / seconds / 1024.0, started);
    }
    return 0;
}
} // namespace

Result Download(const std::string& url, const std::string& path, const Progress& progress,
                s64 expected_size, int connections) {
    if (expected_size <= 0) {
        return DownloadSingle(url, path, progress);
    }
    if (connections <= 0) {
        connections = expected_size >= kParallelThreshold ? kParallelConnections : 1;
    }
    if (connections <= 1) {
        return DownloadSingle(url, path, progress);
    }

    bool retry_single = false;
    bool cancelled = false;
    const Result rc = DownloadRanges(url, path, expected_size, connections, progress, retry_single,
                                     cancelled);
    if (R_SUCCEEDED(rc) && !retry_single) {
        return 0;
    }
    if (cancelled) {
        return rc;
    }
    if (retry_single) {
        log_write("[http] parallel download failed, retrying single connection\n");
        return DownloadSingle(url, path, progress);
    }
    return rc;
}

Result DownloadStream(const std::string& url, const DataCallback& on_data, const Progress& progress) {
    if (!Init()) {
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }

    ProgressCtx ctx{&progress, nullptr, false};
    CURL* curl = MakeHandle(url, &ctx);
    if (curl == nullptr) {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStream);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &on_data);

    const CURLcode rc = curl_easy_perform(curl);
    long code{};
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || code >= 400) {
        log_write("[http] %s failed: %s code %ld\n", url.c_str(), curl_easy_strerror(rc), code);
        if (ctx.aborted) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    return 0;
}

Result Get(const std::string& url, std::vector<u8>& out, const Cancel& cancel,
           size_t max_size) {
    if (!Init()) {
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }

    ProgressCtx ctx{};
    ctx.cancel = &cancel;
    CURL* curl = MakeHandle(url, &ctx);
    if (curl == nullptr) {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    MemoryContext memory{&out, max_size, false};
    out.clear();
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &memory);

    const CURLcode rc = curl_easy_perform(curl);
    long code{};
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || code >= 400) {
        log_write("[http] %s failed: %s code %ld\n", url.c_str(), curl_easy_strerror(rc), code);
        if (memory.too_large) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    return 0;
}

} // namespace launcher::http
