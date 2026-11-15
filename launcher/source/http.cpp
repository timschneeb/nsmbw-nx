#include "http.hpp"

#include "log.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <cstring>

namespace launcher::http {
namespace {
bool g_init{};

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

size_t WriteMemory(void* data, size_t size, size_t count, void* userdata) {
    auto* out = static_cast<std::vector<u8>*>(userdata);
    const size_t bytes = size * count;
    const auto* src = static_cast<const u8*>(data);
    out->insert(out->end(), src, src + bytes);
    return bytes;
}

struct ProgressCtx {
    const Progress* progress{};
    const std::atomic_bool* cancel{};
    bool aborted{};
};

int OnProgress(void* userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* ctx = static_cast<ProgressCtx*>(userdata);
    if (ctx->cancel != nullptr && ctx->cancel->load()) {
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
    // The console has no CA bundle, same as sphaira.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    return curl;
}
} // namespace

bool Init() {
    if (g_init) {
        return true;
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        log_write("[http] curl_global_init failed\n");
        return false;
    }
    g_init = true;
    return true;
}

void Exit() {
    if (g_init) {
        curl_global_cleanup();
        g_init = false;
    }
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

Result Get(const std::string& url, std::vector<u8>& out, const std::atomic_bool* cancel) {
    if (!Init()) {
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }

    ProgressCtx ctx{};
    ctx.cancel = cancel;
    CURL* curl = MakeHandle(url, &ctx);
    if (curl == nullptr) {
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }

    out.clear();
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemory);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);

    const CURLcode rc = curl_easy_perform(curl);
    long code{};
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || code >= 400) {
        log_write("[http] %s failed: %s code %ld\n", url.c_str(), curl_easy_strerror(rc), code);
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    return 0;
}

} // namespace launcher::http
