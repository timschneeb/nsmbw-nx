#include "install.hpp"

#include "app.hpp"
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
#include <vector>

#include <switch/runtime/devices/fs_dev.h>

namespace launcher {
namespace {
namespace fs = std::filesystem;

constexpr s64 kInstallHeadroom = 32 * 1024 * 1024;

auto Lowercase(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

auto StaleNro(const Component& component, const InstalledComponent& installed) -> bool {
    if (!component.Valid()) {
        return false;
    }
    if (component.recomp_version.empty() || installed.recomp_version.empty() ||
        CompareVersion(component.recomp_version, installed.recomp_version) != 0) {
        return true;
    }
    if (!component.game_version.empty() &&
        (installed.game_version.empty() ||
         CompareVersion(component.game_version, installed.game_version) != 0)) {
        return true;
    }
    return !component.region.empty() && component.region != installed.region;
}

auto StaleRomfs(const Component& component, const InstalledComponent& installed) -> bool {
    if (!component.Valid()) {
        return false;
    }
    if (component.game_version.empty() || installed.game_version.empty() ||
        CompareVersion(component.game_version, installed.game_version) != 0) {
        return true;
    }
    return !component.region.empty() && component.region != installed.region;
}

auto FileSize(const std::string& path) -> s64 {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    return ec ? -1 : static_cast<s64>(size);
}

auto Exists(const std::string& path) -> bool {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

auto Megabytes(s64 bytes) -> long long {
    return (bytes + (1024 * 1024) - 1) / (1024 * 1024);
}

auto Fail(std::string& error, std::string message) -> Result {
    error = std::move(message);
    log_write("[install] %s\n", error.c_str());
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

Result Verify(const std::string& path, const std::string& sha, std::string& error,
              const std::function<bool()>& canceled = {}, const ShaProgress& progress = {}) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return Fail(error, "Missing " + fs::path(path).filename().string() + ".");
    }
    if (sha.empty()) {
        return 0;
    }

    std::string actual;
    Result rc = FileSha256(path, actual, canceled, progress);
    if (R_FAILED(rc)) {
        if (canceled && canceled()) {
            return Fail(error, "Install cancelled.");
        }
        return Fail(error, "Could not read " + fs::path(path).filename().string() + ".");
    }
    if (Lowercase(actual) != Lowercase(sha)) {
        log_write("[install] sha mismatch for %s: %s\n", path.c_str(), actual.c_str());
        return Fail(error, "Checksum mismatch for " + fs::path(path).filename().string() + ".");
    }
    return 0;
}

Result CopyLocal(const std::string& src, const std::string& dest, ui::ProgressBox& pbox,
                 std::string& error, bool cancellable = true) {
    FILE* in = std::fopen(src.c_str(), "rb");
    if (in == nullptr) {
        return Fail(error, "Cannot read " + fs::path(src).filename().string() + ".");
    }

    FILE* out = std::fopen(dest.c_str(), "wb");
    if (out == nullptr) {
        std::fclose(in);
        return Fail(error, "Cannot write " + fs::path(dest).filename().string() + ".");
    }

    const s64 size = FileSize(src);
    s64 offset = 0;

    std::vector<u8> buffer(1024 * 1024);
    bool failed = false;
    while (true) {
        if (cancellable && pbox.ShouldExit()) {
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
        if (size > 0) {
            pbox.UpdateTransfer(offset, size);
        }
    }

    failed = failed || std::ferror(in) != 0;
    std::fclose(in);
    const bool closed = std::fclose(out) == 0;

    if (failed || !closed) {
        std::remove(dest.c_str());
        if (cancellable && pbox.ShouldExit()) {
            return Fail(error, "Install cancelled.");
        }
        return Fail(error, "Could not write " + fs::path(dest).filename().string() + ".");
    }

    return 0;
}

Result DownloadVerified(const std::string& url, const std::string& dest, const std::string& sha,
                        s64 expected_size, const std::function<bool(const std::string&)>& validate,
                        const std::string& invalid_error, ui::ProgressBox& pbox, std::string& error,
                        bool commit = true) {
    const std::string part = dest + ".part";
    const auto name = fs::path(dest).filename().string();
    pbox.NewTransfer("Downloading " + name);

    Result rc = http::Download(url, part, [&pbox](s64 now, s64 total) {
        if (total > 0) {
            pbox.UpdateTransfer(now, total);
        }
        return !pbox.ShouldExit();
    }, expected_size);

    if (R_FAILED(rc)) {
        std::error_code ec;
        fs::remove(part, ec);
        if (pbox.ShouldExit()) {
            return Fail(error, "Install cancelled.");
        }
        return Fail(error, "Download failed for " + name + ".");
    }

    std::error_code ec;
    if (expected_size > 0 && FileSize(part) != expected_size) {
        fs::remove(part, ec);
        return Fail(error, "Downloaded " + name + " has the wrong size.");
    }

    if (expected_size > 0) {
        pbox.UpdateTransfer(expected_size, expected_size);
    }
    pbox.SetTransfer("Verifying " + name);
    rc = Verify(part, sha, error, [&pbox] { return pbox.ShouldExit(); },
                [&pbox](s64 now, s64 total) {
                    if (!pbox.IsCombined()) {
                        pbox.UpdateTransfer(now, total);
                    }
                });
    if (R_FAILED(rc)) {
        fs::remove(part, ec);
        return rc;
    }

    if (validate && !validate(part)) {
        fs::remove(part, ec);
        return Fail(error, invalid_error);
    }

    if (pbox.ShouldExit()) {
        fs::remove(part, ec);
        return Fail(error, "Install cancelled.");
    }

    if (!commit) {
        return 0;
    }

    fs::remove(dest, ec);
    fs::rename(part, dest, ec);
    if (ec) {
        log_write("[install] rename failed %s: %s\n", dest.c_str(), ec.message().c_str());
        fs::remove(part, ec);
        return Fail(error, "Could not place " + name + ".");
    }

    return 0;
}

Result Obtain(const Component& component, const std::string& dest, ui::ProgressBox& pbox,
              std::string& error, const std::function<bool(const std::string&)>& validate = {},
              const std::string& invalid_error = {}, bool commit = true) {
    if (!component.path.empty()) {
        std::error_code ec;
        const auto name = fs::path(dest).filename().string();

        // A local component may already be the destination (manual catalogue).
        if (Lowercase(component.path) == Lowercase(dest)) {
            if (!Exists(dest)) {
                if (!component.url.empty()) {
                    return DownloadVerified(component.url, dest, component.sha256, component.size,
                                            validate, invalid_error, pbox, error, commit);
                }
                return Fail(error, "Missing " + name + ".");
            }
            pbox.NewTransfer("Verifying " + name);
            Result rc = Verify(dest, component.sha256, error, [&pbox] { return pbox.ShouldExit(); },
                               [&pbox](s64 now, s64 total) {
                                   if (!pbox.IsCombined()) {
                                       pbox.UpdateTransfer(now, total);
                                   }
                               });
            if (R_FAILED(rc)) {
                return rc;
            }
            if (validate && !validate(dest)) {
                return Fail(error, invalid_error);
            }
            return 0;
        }

        const std::string part = dest + ".part";
        pbox.NewTransfer("Copying " + name);
        Result rc = CopyLocal(component.path, part, pbox, error);
        if (R_FAILED(rc)) {
            return rc;
        }

        if (component.size > 0 && FileSize(part) != component.size) {
            fs::remove(part, ec);
            return Fail(error, "Copied " + name + " has the wrong size.");
        }

        if (component.size > 0) {
            pbox.UpdateTransfer(component.size, component.size);
        }
        pbox.SetTransfer("Verifying " + name);
        rc = Verify(part, component.sha256, error, [&pbox] { return pbox.ShouldExit(); },
                    [&pbox](s64 now, s64 total) {
                        if (!pbox.IsCombined()) {
                            pbox.UpdateTransfer(now, total);
                        }
                    });
        if (R_FAILED(rc)) {
            fs::remove(part, ec);
            return rc;
        }

        if (validate && !validate(part)) {
            fs::remove(part, ec);
            return Fail(error, invalid_error);
        }

        if (pbox.ShouldExit()) {
            fs::remove(part, ec);
            return Fail(error, "Install cancelled.");
        }

        if (!commit) {
            return 0;
        }

        fs::remove(dest, ec);
        fs::rename(part, dest, ec);
        if (ec) {
            fs::remove(part, ec);
            return Fail(error, "Could not place " + name + ".");
        }
        return 0;
    }

    if (component.url.empty()) {
        return Fail(error, "No download source for " + fs::path(dest).filename().string() + ".");
    }
    return DownloadVerified(component.url, dest, component.sha256, component.size, validate,
                            invalid_error, pbox, error, commit);
}

auto NeedsNro(const GameEntry& entry, const AssetsGame& catalog, bool force) -> bool {
    const Installed installed = ReadInstalled(entry.dir);
    if (force || StaleNro(catalog.nro, installed.nro) ||
        (!Exists(entry.nro_path) && catalog.nro.Valid()) ||
        (Exists(entry.nro_path) && !IsValidNro(entry.nro_path))) {
        return true;
    }
    return catalog.nro.size > 0 && FileSize(entry.nro_path) != catalog.nro.size;
}

auto NeedsRomfs(const GameEntry& entry, const AssetsGame& catalog, bool force) -> bool {
    if (!catalog.romfs.Valid()) {
        return false;
    }

    const Installed installed = ReadInstalled(entry.dir);
    if (force || StaleRomfs(catalog.romfs, installed.romfs)) {
        return true;
    }
    if (installed.romfs.size > 0 && FileSize(entry.romfs_path) != installed.romfs.size) {
        return true;
    }
    return catalog.romfs.image_size > 0 && FileSize(entry.romfs_path) != catalog.romfs.image_size;
}

auto RequiredFreeSpace(const GameEntry& entry, const AssetsGame& catalog, bool nro, bool romfs) -> s64 {
    s64 needed = kInstallHeadroom;
    if (nro && catalog.nro.Valid()) {
        s64 next = catalog.nro.size;
        if (next <= 0 && !catalog.nro.path.empty()) {
            next = FileSize(catalog.nro.path);
        }
        // A local component that is already in place is only verified.
        const bool in_place = !catalog.nro.path.empty() &&
                              Lowercase(catalog.nro.path) == Lowercase(entry.nro_path);
        if (in_place) {
            next = 0;
        } else {
            // The old file stays in place until the verified download replaces it.
            needed += std::max<s64>(FileSize(entry.nro_path), 0);
        }
        needed += std::max<s64>(next, 0);
    }
    if (romfs) {
        needed += std::max<s64>(catalog.romfs.image_size, 0);
        needed += std::max<s64>(FileSize(entry.romfs_path), 0);
    }
    return needed;
}

Result CheckFreeSpace(const GameEntry& entry, const AssetsGame& catalog, bool nro, bool romfs,
                      std::string& error) {
    const s64 needed = RequiredFreeSpace(entry, catalog, nro, romfs);

    s64 free = 0;
    s64 total = 0;
    if (R_FAILED(App::GetSdSize(&free, &total))) {
        return Fail(error, "Could not read the SD card free space.");
    }
    if (needed > free) {
        return Fail(error, "Not enough space. Need " + std::to_string(Megabytes(needed)) +
                               " MB, " + std::to_string(Megabytes(free)) + " MB free.");
    }
    return 0;
}

void RemoveIfEmpty(const std::string& path) {
    std::error_code ec;
    if (fs::exists(path, ec) && !ec && fs::is_empty(path, ec) && !ec) {
        fs::remove(path, ec);
    }
}

Result InstallGameInner(const GameEntry& entry, const AssetsGame& catalog, ui::ProgressBox& pbox,
                        std::string& error, bool force) {
    error.clear();
    Installed installed = ReadInstalled(entry.dir);
    if (installed.name.empty()) {
        installed.name = catalog.name;
    }

    const bool nro = NeedsNro(entry, catalog, force);
    const bool romfs = NeedsRomfs(entry, catalog, force);
    log_write("[install] %s force=%d present=%d nro=%d romfs=%d\n", entry.id.c_str(), force,
              installed.present, nro, romfs);

    if (!catalog.nro.Valid() && !Exists(entry.nro_path)) {
        return Fail(error, "No game.nro is available for " + entry.name + ".");
    }
    Result rc = CheckFreeSpace(entry, catalog, nro, romfs, error);
    if (R_FAILED(rc)) {
        return rc;
    }

    std::error_code ec;
    fs::create_directories(entry.dir, ec);

    const std::string nro_part = entry.nro_path + ".part";
    const std::string romfs_part = entry.romfs_path + ".part";
    const auto cleanup_parts = [&] {
        std::error_code cleanup_ec;
        fs::remove(nro_part, cleanup_ec);
        fs::remove(romfs_part, cleanup_ec);
    };

    const bool nro_in_place = !catalog.nro.path.empty() && Exists(entry.nro_path) &&
                              Lowercase(catalog.nro.path) == Lowercase(entry.nro_path);
    const bool stage_nro = nro && !nro_in_place;
    const bool stage_romfs = romfs;

    s64 combined_total = 0;
    bool combined = stage_nro || stage_romfs;
    if (stage_nro) {
        if (catalog.nro.size > 0) {
            combined_total += catalog.nro.size;
        } else {
            combined = false;
        }
    }
    if (stage_romfs) {
        if (catalog.romfs.image_size > 0) {
            combined_total += catalog.romfs.image_size;
        } else {
            combined = false;
        }
    }
    if (combined && combined_total > 0) {
        pbox.SetCombinedProgress(combined_total);
    }

    Installed updated = installed;

    if (nro) {
        if (catalog.nro.sha256.empty()) {
            cleanup_parts();
            return Fail(error, "The catalogue has no checksum for game.nro.");
        }
        pbox.SetProgressBase(0);
        rc = Obtain(catalog.nro, entry.nro_path, pbox, error, IsValidNro,
                    "The downloaded game file is not a valid NRO.", !stage_nro);
        if (R_FAILED(rc)) {
            cleanup_parts();
            return rc;
        }
        if (pbox.ShouldExit()) {
            cleanup_parts();
            return Fail(error, "Install cancelled.");
        }
        updated.nro.recomp_version = catalog.nro.recomp_version;
        updated.nro.game_version = catalog.nro.game_version;
        updated.nro.region = catalog.nro.region;
        updated.nro.sha256 = catalog.nro.sha256;
        updated.nro.size = catalog.nro.size > 0
                               ? catalog.nro.size
                               : FileSize(stage_nro ? nro_part : entry.nro_path);
    }

    if (romfs) {
        if (catalog.romfs.sha256.empty() && catalog.romfs.image_sha256.empty()) {
            cleanup_parts();
            return Fail(error, "The catalogue has no checksum for assets.romfs.");
        }
        if (pbox.ShouldExit()) {
            cleanup_parts();
            return Fail(error, "Install cancelled.");
        }
        if (combined) {
            pbox.SetProgressBase(stage_nro ? catalog.nro.size : 0);
        }
        rc = InstallRomfs(catalog.romfs, entry.romfs_path, pbox, error, !stage_romfs);
        if (R_FAILED(rc)) {
            cleanup_parts();
            return rc;
        }
        updated.romfs.recomp_version.clear();
        updated.romfs.game_version = catalog.romfs.game_version;
        updated.romfs.region = catalog.romfs.region;
        updated.romfs.sha256 = catalog.romfs.image_sha256;
        updated.romfs.size = catalog.romfs.image_size > 0 ? catalog.romfs.image_size
                                                          : FileSize(romfs_part);
    }

    if (stage_nro) {
        fs::remove(entry.nro_path, ec);
        fs::rename(nro_part, entry.nro_path, ec);
        if (ec) {
            log_write("[install] nro commit failed %s: %s\n", entry.nro_path.c_str(),
                      ec.message().c_str());
            cleanup_parts();
            return Fail(error, "Could not place game.nro.");
        }
    }
    if (stage_romfs) {
        fs::remove(entry.romfs_path, ec);
        fs::rename(romfs_part, entry.romfs_path, ec);
        if (ec) {
            log_write("[install] romfs commit failed %s: %s\n", entry.romfs_path.c_str(),
                      ec.message().c_str());
            cleanup_parts();
            return Fail(error, "Could not place assets.romfs.");
        }
    }
    if (nro || romfs) {
        if (R_FAILED(WriteInstalled(entry.dir, updated))) {
            cleanup_parts();
            return Fail(error, "Could not write installed.json.");
        }
    }

    log_write("[install] %s done\n", entry.id.c_str());
    return 0;
}

auto ComponentDownloadSize(const Component& component) -> s64 {
    if (component.size > 0) {
        return component.size;
    }
    if (!component.path.empty()) {
        return std::max<s64>(FileSize(component.path), 0);
    }
    return 0;
}

} // namespace

Result InstallGame(const GameEntry& entry, const AssetsGame& catalog, ui::ProgressBox& pbox,
                   std::string& error, bool force) {
    std::error_code ec;
    const bool existed = fs::exists(entry.dir, ec) && !ec;
    const Result rc = InstallGameInner(entry, catalog, pbox, error, force);
    if (R_FAILED(rc) && !existed) {
        RemoveIfEmpty(entry.dir);
    }
    return rc;
}

auto InstallTransferSize(const GameEntry& entry, const AssetsGame& catalog, bool force) -> s64 {
    s64 total = 0;
    const bool nro_in_place = !catalog.nro.path.empty() && Exists(entry.nro_path) &&
                              Lowercase(catalog.nro.path) == Lowercase(entry.nro_path);
    if (NeedsNro(entry, catalog, force) && !nro_in_place) {
        total += ComponentDownloadSize(catalog.nro);
    }
    if (NeedsRomfs(entry, catalog, force)) {
        total += ComponentDownloadSize(catalog.romfs);
    }
    return total;
}

auto InstallRequiredSpace(const GameEntry& entry, const AssetsGame& catalog, bool force) -> s64 {
    const bool nro = NeedsNro(entry, catalog, force);
    const bool romfs = NeedsRomfs(entry, catalog, force);
    return RequiredFreeSpace(entry, catalog, nro, romfs);
}

auto InstallPromptMessage(const GameEntry& entry, const AssetsGame& catalog, const std::string& action,
                          bool force) -> std::string {
    const s64 size = InstallTransferSize(entry, catalog, force);
    const s64 required = InstallRequiredSpace(entry, catalog, force);

    s64 free = 0;
    s64 total = 0;
    const bool have_free = R_SUCCEEDED(App::GetSdSize(&free, &total));

    std::string message = action + " " + entry.name + "?\n";
    if (size > 0) {
        message += "Download " + FormatSize(size) + ". ";
    } else {
        message += "The game files are on the SD card. ";
    }
    message += "Needs " + FormatSize(required) + " free";
    if (have_free) {
        message += ", " + FormatSize(free) + " available";
    }
    message += ".";
    return message;
}

Result VerifyGame(const GameEntry& entry, ui::ProgressBox& pbox, std::string& error) {
    error.clear();
    const Installed installed = ReadInstalled(entry.dir);
    if (!installed.present) {
        return Fail(error, "There is no install metadata to verify.");
    }
    if (installed.nro.sha256.empty() && installed.romfs.sha256.empty()) {
        return Fail(error, "No checksums are recorded. Reinstall the game first.");
    }

    const auto check = [&pbox, &error](const std::string& path, const InstalledComponent& component,
                                       const char* label) -> Result {
        if (component.sha256.empty() && component.size <= 0) {
            return 0;
        }
        if (!Exists(path)) {
            return Fail(error, std::string("Missing ") + label + ".");
        }
        if (component.size > 0 && FileSize(path) != component.size) {
            return Fail(error, std::string("Size mismatch for ") + label + ".");
        }
        if (component.sha256.empty()) {
            return 0;
        }

        pbox.NewTransfer(std::string("Verifying ") + label);
        std::string actual;
        const Result rc = FileSha256(path, actual, [&pbox] { return pbox.ShouldExit(); },
                                     [&pbox](s64 now, s64 total) { pbox.UpdateTransfer(now, total); });
        if (R_FAILED(rc)) {
            return Fail(error, pbox.ShouldExit() ? "Install cancelled."
                                                 : std::string("Could not read ") + label + ".");
        }
        if (Lowercase(actual) != Lowercase(component.sha256)) {
            return Fail(error, std::string("Checksum mismatch for ") + label + ".");
        }
        return 0;
    };

    Result rc = check(entry.nro_path, installed.nro, "game.nro");
    if (R_FAILED(rc)) {
        return rc;
    }
    rc = check(entry.romfs_path, installed.romfs, "assets.romfs");
    if (R_FAILED(rc)) {
        return rc;
    }

    log_write("[install] verified %s\n", entry.id.c_str());
    return 0;
}

Result InstallLauncher(const Assets& assets, ui::ProgressBox& pbox, std::string& error, bool& restart) {
    restart = false;
    if (assets.launcher_url.empty() || assets.launcher_version.empty()) {
        return Fail(error, "The catalogue has no launcher update.");
    }

    const std::string path = LauncherPath();
    const std::string staging = path + ".new";
    const std::string backup = path + ".old";

    std::error_code ec;
    fs::remove(staging, ec);
    fs::remove(backup, ec);

    Result rc = DownloadVerified(assets.launcher_url, staging, assets.launcher_sha256,
                                 assets.launcher_size, {}, {}, pbox, error);
    if (R_FAILED(rc)) {
        return rc;
    }

    pbox.NewTransfer("Checking launcher version");
    if (!IsValidNro(staging)) {
        fs::remove(staging, ec);
        return Fail(error, "The downloaded launcher is not an NRO.");
    }

    std::string version;
    if (R_FAILED(ReadNroVersion(staging, version))) {
        fs::remove(staging, ec);
        return Fail(error, "The downloaded launcher has no version.");
    }
    if (CompareVersion(version, assets.launcher_version) != 0) {
        fs::remove(staging, ec);
        return Fail(error, "The download is version " + version + ", expected " + assets.launcher_version + ".");
    }

    pbox.NewTransfer("Backing up launcher");
    rc = CopyLocal(path, backup, pbox, error);
    if (R_FAILED(rc)) {
        fs::remove(staging, ec);
        return rc;
    }

    pbox.NewTransfer("Installing launcher");
    rc = CopyLocal(staging, path, pbox, error);
    if (R_FAILED(rc)) {
        std::string ignored;
        CopyLocal(backup, path, pbox, ignored, false);
        fs::remove(staging, ec);
        return rc;
    }

    pbox.NewTransfer("Verifying launcher");
    if (!IsValidNro(path) || R_FAILED(ReadNroVersion(path, version)) ||
        CompareVersion(version, assets.launcher_version) != 0) {
        std::string ignored;
        CopyLocal(backup, path, pbox, ignored, false);
        fs::remove(staging, ec);
        return Fail(error, "The installed launcher failed verification.");
    }

    fs::remove(staging, ec);
    fs::remove(backup, ec);
    restart = true;
    log_write("[install] launcher %s installed\n", assets.launcher_version.c_str());
    return 0;
}

void CleanupPendingInstalls() {
    std::error_code ec;
    const std::string launcher = LauncherPath();
    fs::remove(launcher + ".part", ec);
    fs::remove(launcher + ".new", ec);
    fs::remove(launcher + ".new.part", ec);
    fs::remove(launcher + ".old", ec);
    fs::remove((fs::path(RootDirectory()) / "assets.cache.json.tmp").string(), ec);
    fs::remove((fs::path(RootDirectory()) / "usage.json.tmp").string(), ec);

    const std::string banners = BannersDirectory();
    fs::create_directories(banners, ec);
    std::error_code remove_ec;
    for (fs::directory_iterator it(banners, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().extension() == ".part") {
            fs::remove(it->path(), remove_ec);
        }
    }

    fs::path games = GamesDirectory();
    std::vector<fs::path> trash;
    for (fs::directory_iterator it(games, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) {
            continue;
        }
        if (it->path().filename().string().ends_with(".removed")) {
            trash.push_back(it->path());
            continue;
        }
        fs::remove(it->path() / "game.nro.part", remove_ec);
        fs::remove(it->path() / "assets.romfs.part", remove_ec);
        fs::remove(it->path() / "game.nro.new", remove_ec);
        fs::remove(it->path() / "assets.romfs.new", remove_ec);
        fs::remove(it->path() / "installed.json.tmp", remove_ec);
    }

    for (const auto& path : trash) {
        const Result rc = fsdevDeleteDirectoryRecursively(path.string().c_str());
        if (R_FAILED(rc)) {
            log_write("[install] leftover trash %s: 0x%08X\n", path.c_str(),
                      static_cast<unsigned>(rc));
        }
    }
}

auto InstallDownloadSize(const AssetsGame& game) -> s64 {
    s64 total = 0;
    if (game.nro.Valid()) {
        total += ComponentDownloadSize(game.nro);
    }
    if (game.romfs.Valid()) {
        total += ComponentDownloadSize(game.romfs);
    }
    return total;
}

auto FormatSize(s64 bytes) -> std::string {
    if (bytes <= 0) {
        return "unknown size";
    }

    static constexpr const char* kUnits[] = {"B", "KB", "MB", "GB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        unit++;
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), unit == 0 ? "%.0f %s" : "%.1f %s", value, kUnits[unit]);
    return buffer;
}

} // namespace launcher
