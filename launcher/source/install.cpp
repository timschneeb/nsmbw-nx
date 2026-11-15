#include "install.hpp"

#include "http.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "romfs.hpp"
#include "sha.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace launcher {
namespace {
namespace fs = std::filesystem;

auto Lowercase(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

auto Stale(const Component& component, const std::string& installed) -> bool {
    return component.Valid() && CompareVersion(component.version, installed) != 0;
}

Result Verify(const std::string& path, const std::string& sha) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        log_write("[install] %s is missing\n", path.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }
    if (sha.empty()) {
        return 0;
    }

    std::string actual;
    Result rc = FileSha256(path, actual);
    if (R_FAILED(rc) || Lowercase(actual) != Lowercase(sha)) {
        log_write("[install] sha mismatch for %s: %s\n", path.c_str(), actual.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    return 0;
}

Result CopyLocal(const std::string& src, const std::string& dest, ui::ProgressBox& pbox) {
    FILE* in = std::fopen(src.c_str(), "rb");
    if (in == nullptr) {
        log_write("[install] cannot open %s\n", src.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }

    FILE* out = std::fopen(dest.c_str(), "wb");
    if (out == nullptr) {
        std::fclose(in);
        log_write("[install] cannot write %s\n", dest.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    std::error_code ec;
    const s64 size = static_cast<s64>(fs::file_size(src, ec));
    s64 offset = 0;

    std::vector<u8> buffer(1024 * 1024);
    bool failed = false;
    while (true) {
        if (pbox.ShouldExit()) {
            failed = true;
            break;
        }

        const size_t read = std::fread(buffer.data(), 1, buffer.size(), in);
        if (read == 0) {
            break;
        }

        if (std::fwrite(buffer.data(), 1, read, out) != read) {
            failed = true;
            break;
        }
        offset += static_cast<s64>(read);
        pbox.UpdateTransfer(offset, size);
    }

    failed = failed || std::ferror(in) != 0;
    std::fclose(in);
    const bool closed = std::fclose(out) == 0;

    if (failed || !closed) {
        std::remove(dest.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    return 0;
}

Result DownloadVerified(const std::string& url, const std::string& dest, const std::string& sha,
                        s64 size, ui::ProgressBox& pbox) {
    const std::string part = dest + ".part";
    pbox.NewTransfer(fs::path(dest).filename().string());

    Result rc = http::Download(url, part, [&pbox](s64 now, s64 total) {
        if (total > 0) {
            pbox.UpdateTransfer(now, total);
        }
        return !pbox.ShouldExit();
    });

    if (R_FAILED(rc)) {
        std::error_code ec;
        fs::remove(part, ec);
        return rc;
    }

    pbox.NewTransfer("Verifying " + fs::path(dest).filename().string());
    rc = Verify(part, sha);
    if (R_FAILED(rc)) {
        std::error_code ec;
        fs::remove(part, ec);
        return rc;
    }

    std::error_code ec;
    fs::remove(dest, ec);
    fs::rename(part, dest, ec);
    if (ec) {
        log_write("[install] rename failed %s: %s\n", dest.c_str(), ec.message().c_str());
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    return 0;
}

Result Obtain(const Component& component, const std::string& dest, ui::ProgressBox& pbox) {
    if (!component.path.empty()) {
        std::error_code ec;

        // A local component may already be the destination (manual catalogue).
        if (Lowercase(component.path) == Lowercase(dest)) {
            std::error_code missing_ec;
            if (!fs::exists(dest, missing_ec)) {
                if (!component.url.empty()) {
                    return DownloadVerified(component.url, dest, component.sha256, component.size, pbox);
                }
                return MAKERESULT(Module_Libnx, LibnxError_NotFound);
            }
            pbox.NewTransfer("Verifying " + fs::path(dest).filename().string());
            return Verify(dest, component.sha256);
        }

        pbox.NewTransfer(fs::path(dest).filename().string());
        Result rc = CopyLocal(component.path, dest, pbox);
        if (R_FAILED(rc)) {
            return rc;
        }

        pbox.NewTransfer("Verifying " + fs::path(dest).filename().string());
        rc = Verify(dest, component.sha256);
        if (R_FAILED(rc)) {
            fs::remove(dest, ec);
        }
        return rc;
    }

    return DownloadVerified(component.url, dest, component.sha256, component.size, pbox);
}

} // namespace

Result InstallGame(const GameEntry& entry, const AssetsGame& catalog, ui::ProgressBox& pbox, bool force) {
    Installed installed = ReadInstalled(entry.dir);
    log_write("[install] %s force=%d present=%d nro=%s res=%s\n", entry.id.c_str(), force,
              installed.present, installed.nro_version.c_str(), installed.romfs_version.c_str());
    if (!installed.present) {
        installed.name = catalog.name;
    }

    std::error_code ec;
    fs::create_directories(entry.dir, ec);

    if (force || Stale(catalog.nro, installed.nro_version) ||
        (!fs::exists(entry.nro_path, ec) && catalog.nro.Valid())) {
        Result rc = Obtain(catalog.nro, entry.nro_path, pbox);
        if (R_FAILED(rc)) {
            return rc;
        }
        installed.nro_version = catalog.nro.version;
        WriteInstalled(entry.dir, installed);
    }

    if (force || Stale(catalog.romfs, installed.romfs_version)) {
        Result rc = InstallRomfs(catalog.romfs, entry.dir + "/assets.romfs", pbox);
        if (R_FAILED(rc)) {
            return rc;
        }
        installed.romfs_version = catalog.romfs.version;
        WriteInstalled(entry.dir, installed);
    }

    log_write("[install] %s done\n", entry.id.c_str());
    return 0;
}

Result InstallLauncher(const Assets& assets, ui::ProgressBox& pbox) {
    if (assets.launcher_url.empty()) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    const std::string path = LauncherPath();
    const std::string part = path + ".part";
    Result rc = DownloadVerified(assets.launcher_url, part, assets.launcher_sha256,
                                 assets.launcher_size, pbox);
    if (R_FAILED(rc)) {
        return rc;
    }

    std::error_code ec;
    const std::string old = path + ".old";
    fs::remove(old, ec);
    fs::rename(path, old, ec);
    if (ec) {
        log_write("[install] cannot move running launcher: %s\n", ec.message().c_str());
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    fs::rename(part, path, ec);
    if (ec) {
        log_write("[install] cannot place launcher: %s\n", ec.message().c_str());
        fs::rename(old, path, ec);
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    log_write("[install] launcher %s installed\n", assets.launcher_version.c_str());
    return 0;
}

void CleanupPendingInstalls() {
    std::error_code ec;
    const std::string launcher = LauncherPath();
    fs::remove(launcher + ".part", ec);
    fs::remove(launcher + ".old", ec);

    fs::path games = GamesDirectory();
    for (fs::directory_iterator it(games, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) {
            continue;
        }
        fs::remove(it->path() / "game.nro.part", ec);
        fs::remove(it->path() / "assets.romfs.part", ec);
        // leftovers from the ZIP based installer.
        std::filesystem::remove_all(it->path() / "data.new", ec);
        std::filesystem::remove_all(it->path() / "data.old", ec);
        fs::remove(it->path() / "resources.zip", ec);
    }
}

} // namespace launcher
