#include "switch.h"
#include "switch/runtime/devices/fs_dev.h"

#include "app.hpp"
#include "paths.hpp"
#include "test_hooks.hpp"

#include <openssl/evp.h>

#include <cerrno>
#include <filesystem>
#include <system_error>
#include <unistd.h>

// http.cpp expects the embedded CA bundle. Tests use file:// and loopback HTTP URLs.
// Point both linker symbols at the same empty object so the pointer difference is zero.
extern "C" {
extern const unsigned char cacert_pem[1];
const unsigned char cacert_pem[1] = {0};
}
asm(".globl cacert_pem\n.globl cacert_pem_end\n.set cacert_pem_end, cacert_pem\n");

void sha256ContextCreate(Sha256Context* ctx) {
    ctx->ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx->ctx, EVP_sha256(), nullptr);
}

void sha256ContextUpdate(Sha256Context* ctx, const void* data, size_t size) {
    EVP_DigestUpdate(ctx->ctx, data, size);
}

void sha256ContextGetHash(Sha256Context* ctx, u8 hash[32]) {
    unsigned int length = 0;
    EVP_DigestFinal_ex(ctx->ctx, hash, &length);
    EVP_MD_CTX_free(ctx->ctx);
    ctx->ctx = nullptr;
}

namespace launcher {

auto App::GetSdSize(s64* free, s64* total) -> Result {
    if (test::g_sd_size.fail) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    if (test::g_sd_size.free >= 0) {
        *free = test::g_sd_size.free;
        *total = test::g_sd_size.total >= 0 ? test::g_sd_size.total : test::g_sd_size.free;
        return 0;
    }

    std::error_code ec;
    const auto space = std::filesystem::space(RootDirectory(), ec);
    if (ec) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    *free = static_cast<s64>(space.available);
    *total = static_cast<s64>(space.capacity);
    return 0;
}

} // namespace launcher

Result fsdevCreateFile(const char* path, size_t size, u32) {
    (void)size;
    FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    std::fclose(file);
    return 0;
}

Result fsdevDeleteDirectoryRecursively(const char* path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return ec ? MAKERESULT(Module_Libnx, LibnxError_IoError) : 0;
}

int fsdevTranslatePath(const char* path, FsFileSystem** device, char* outpath) {
    *device = reinterpret_cast<FsFileSystem*>(1);
    std::strncpy(outpath, path, FS_MAX_PATH - 1);
    outpath[FS_MAX_PATH - 1] = '\0';
    return 0;
}

Result fsFsOpenFile(FsFileSystem*, const char* path, u32, FsFile* out) {
    out->handle = std::fopen(path, "r+b");
    return out->handle != nullptr ? 0 : MAKERESULT(Module_Libnx, LibnxError_IoError);
}

Result fsFileSetSize(FsFile* file, s64 size) {
    if (ftruncate(fileno(file->handle), size) != 0) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    return 0;
}

Result fsFileWrite(FsFile* file, s64 offset, const void* data, u64 size, u32) {
    if (fseeko(file->handle, offset, SEEK_SET) != 0) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    if (std::fwrite(data, 1, static_cast<size_t>(size), file->handle) != size) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    return 0;
}

void fsFileClose(FsFile* file) {
    if (file->handle != nullptr) {
        std::fclose(file->handle);
        file->handle = nullptr;
    }
}
