#include "romfs.hpp"

#include "http.hpp"
#include "log.hpp"
#include "ui/progress_box.hpp"

#include <switch/runtime/devices/fs_dev.h>

#include <zstd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

namespace launcher {
namespace {
namespace fs = std::filesystem;

auto Lowercase(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

auto Hex(const u8* hash) -> std::string {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (int i = 0; i < 32; i++) {
        out.push_back(digits[hash[i] >> 4]);
        out.push_back(digits[hash[i] & 0xF]);
    }
    return out;
}

struct Writer {
    FsFile file{};
    bool open{};
    s64 offset{};

    Result Open(const std::string& path, s64 size) {
        Result rc = fsdevCreateFile(path.c_str(), size, 0);
        if (R_FAILED(rc)) {
            return rc;
        }

        FsFileSystem* device = nullptr;
        char fs_path[FS_MAX_PATH]{};
        if (fsdevTranslatePath(path.c_str(), &device, fs_path) != 0 || device == nullptr) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

        rc = fsFsOpenFile(device, fs_path, FsOpenMode_Write, &file);
        if (R_FAILED(rc)) {
            return rc;
        }

        rc = fsFileSetSize(&file, size);
        if (R_FAILED(rc)) {
            fsFileClose(&file);
            return rc;
        }

        open = true;
        return 0;
    }

    Result Write(const void* data, size_t size) {
        Result rc = fsFileWrite(&file, offset, data, size, FsWriteOption_None);
        if (R_SUCCEEDED(rc)) {
            offset += static_cast<s64>(size);
        }
        return rc;
    }

    void Close() {
        if (open) {
            fsFileClose(&file);
            open = false;
        }
    }
};

struct FeedState {
    ZSTD_DStream* stream{};
    Writer* writer{};
    ui::ProgressBox* pbox{};
    Sha256Context compressed{};
    Sha256Context image{};
    s64 image_size{};
    s64 written{};
    Result result{0};
    std::string* error{};
    std::vector<u8> output{};

    auto Update(const u8* data, size_t size) -> bool {
        sha256ContextUpdate(&compressed, data, size);

        ZSTD_inBuffer input{data, size, 0};
        while (input.pos < input.size) {
            if (pbox->ShouldExit()) {
                *error = "Install cancelled.";
                result = MAKERESULT(Module_Libnx, LibnxError_BadInput);
                return false;
            }

            ZSTD_outBuffer out{output.data(), output.size(), 0};
            const size_t rc = ZSTD_decompressStream(stream, &out, &input);
            if (ZSTD_isError(rc)) {
                log_write("[romfs] zstd: %s\n", ZSTD_getErrorName(rc));
                *error = "The resource image is corrupt.";
                result = MAKERESULT(Module_Libnx, LibnxError_BadInput);
                return false;
            }

            if (out.pos > 0) {
                sha256ContextUpdate(&image, output.data(), out.pos);
                const Result write_rc = writer->Write(output.data(), out.pos);
                if (R_FAILED(write_rc)) {
                    *error = "Could not write assets.romfs.";
                    result = write_rc;
                    return false;
                }

                written += static_cast<s64>(out.pos);
                pbox->UpdateTransfer(written, image_size);
            }
        }

        return true;
    }
};

auto Verify(const Component& component, Sha256Context& compressed, Sha256Context& image,
            s64 written) -> Result {
    if (!component.sha256.empty()) {
        u8 hash[32];
        sha256ContextGetHash(&compressed, hash);
        const auto actual = Hex(hash);
        if (Lowercase(actual) != Lowercase(component.sha256)) {
            log_write("[romfs] compressed sha mismatch: %s\n", actual.c_str());
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
    }

    if (!component.image_sha256.empty()) {
        u8 hash[32];
        sha256ContextGetHash(&image, hash);
        const auto actual = Hex(hash);
        if (Lowercase(actual) != Lowercase(component.image_sha256)) {
            log_write("[romfs] image sha mismatch: %s\n", actual.c_str());
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
    }

    if (component.image_size > 0 && written != component.image_size) {
        log_write("[romfs] image size %lld, expected %lld\n", static_cast<long long>(written),
                  static_cast<long long>(component.image_size));
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    return 0;
}
} // namespace

Result InstallRomfs(const Component& component, const std::string& dest, ui::ProgressBox& pbox,
                    std::string& error) {
    error.clear();
    if (component.image_size <= 0) {
        error = "The catalogue has no resource image size.";
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    const std::string part = dest + ".part";
    std::error_code ec;

    Writer writer;
    Result rc = writer.Open(part, component.image_size);
    if (R_FAILED(rc)) {
        log_write("[romfs] cannot create %s: 0x%08X\n", part.c_str(), static_cast<unsigned>(rc));
        error = "Could not create assets.romfs. Check the free space.";
        return rc;
    }

    FeedState state;
    state.stream = ZSTD_createDStream();
    state.writer = &writer;
    state.pbox = &pbox;
    state.image_size = component.image_size;
    state.error = &error;
    state.output.resize(1024 * 1024);
    sha256ContextCreate(&state.compressed);
    sha256ContextCreate(&state.image);

    if (state.stream == nullptr) {
        writer.Close();
        fs::remove(part, ec);
        error = "Not enough memory to decompress the resource image.";
        return MAKERESULT(Module_Libnx, LibnxError_OutOfMemory);
    }
    ZSTD_initDStream(state.stream);

    pbox.NewTransfer(fs::path(dest).filename().string());
    pbox.UpdateTransfer(0, component.image_size);

    if (!component.path.empty()) {
        pbox.SetTransfer("Decompressing " + fs::path(dest).filename().string());
        FILE* file = std::fopen(component.path.c_str(), "rb");
        if (file == nullptr) {
            rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
            error = "Cannot read the resource image.";
        } else {
            std::vector<u8> buffer(1024 * 1024);
            rc = 0;
            while (R_SUCCEEDED(rc)) {
                const size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
                if (read == 0) {
                    break;
                }
                if (!state.Update(buffer.data(), read)) {
                    rc = state.result;
                    break;
                }
            }
            if (R_SUCCEEDED(rc) && std::ferror(file)) {
                rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
                error = "Could not read the resource image.";
            }
            std::fclose(file);
        }
    } else {
        pbox.SetTransfer("Downloading " + fs::path(dest).filename().string());
        rc = http::DownloadStream(component.url, [&state](const u8* data, size_t size) {
            return state.Update(data, size);
        }, nullptr);
        if (R_FAILED(rc) && error.empty()) {
            error = pbox.ShouldExit() ? "Install cancelled." : "Download failed for assets.romfs.";
        }
    }

    writer.Close();
    ZSTD_freeDStream(state.stream);

    if (R_FAILED(rc)) {
        fs::remove(part, ec);
        return rc;
    }

    rc = Verify(component, state.compressed, state.image, state.written);
    if (R_FAILED(rc)) {
        error = "The resource image failed verification.";
        log_write("[romfs] verification failed for %s\n", dest.c_str());
        fs::remove(part, ec);
        return rc;
    }

    fs::remove(dest, ec);
    fs::rename(part, dest, ec);
    if (ec) {
        log_write("[romfs] rename failed %s: %s\n", dest.c_str(), ec.message().c_str());
        fs::remove(part, ec);
        error = "Could not place assets.romfs.";
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    log_write("[romfs] wrote %lld bytes to %s\n", static_cast<long long>(state.written), dest.c_str());
    return 0;
}

} // namespace launcher
