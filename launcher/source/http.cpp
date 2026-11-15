#include "http.hpp"

#include "log.hpp"
#include "paths.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>

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
};

int OnProgress(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ProgressCtx*>(userdata);
    if (ctx->cancel != nullptr && *ctx->cancel && (*ctx->cancel)()) {
        ctx->aborted = true;
        return 1;
    }
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
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 1024L * 256L);
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

Result Download(const std::string& url, const std::string& path, const Progress& progress) {
    if (!Init()) {
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }

    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        log_write("[http] cannot open %s\n", path.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    ProgressCtx ctx{&progress, nullptr, false};
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

    return 0;
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
