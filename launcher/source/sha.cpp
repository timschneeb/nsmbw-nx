#include "sha.hpp"

#include "log.hpp"

#include <cstdio>
#include <filesystem>
#include <vector>

namespace launcher {

Result FileSha256(const std::string& path, std::string& hex, const std::function<bool()>& canceled,
                  const ShaProgress& progress) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }

    std::error_code size_ec;
    const s64 total = static_cast<s64>(std::filesystem::file_size(path, size_ec));
    Sha256Context ctx;
    sha256ContextCreate(&ctx);

    std::vector<u8> buffer(1024 * 1024);
    s64 done = 0;
    size_t read{};
    while ((read = std::fread(buffer.data(), 1, buffer.size(), file)) > 0) {
        if (canceled && canceled()) {
            std::fclose(file);
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        sha256ContextUpdate(&ctx, buffer.data(), read);
        done += static_cast<s64>(read);
        if (progress && total > 0) {
            progress(done, total);
        }
    }

    const bool failed = std::ferror(file) != 0;
    std::fclose(file);
    if (failed) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    u8 hash[32];
    sha256ContextGetHash(&ctx, hash);

    static constexpr char digits[] = "0123456789abcdef";
    hex.clear();
    hex.reserve(sizeof(hash) * 2);
    for (u8 byte : hash) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0xF]);
    }

    log_write("[sha] %s %s\n", path.c_str(), hex.c_str());
    return 0;
}

} // namespace launcher
