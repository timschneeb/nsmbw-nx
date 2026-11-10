#pragma once

#include "runtime_config.h"
#include "nand_settings.h"
#include "runtime_log.h"
#include "system_bridge.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace RuntimeNandPath {

inline std::optional<std::filesystem::path> ExistingDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    if (!path.empty() && std::filesystem::is_directory(path, ec) && !ec) {
        return path;
    }
    return std::nullopt;
}

[[noreturn]] inline void FailNandRoot(const char* message, const std::filesystem::path& path = {}) {
    if (path.empty()) {
        RT_LOGF(RT_TAG_NAND, "ERROR: %s\n", message);
    } else {
        RT_LOGF(RT_TAG_NAND, "ERROR: %s: %s\n", message,
                RuntimeConfigFile::PathToUtf8(path).c_str());
    }
    RT_LOGF(RT_TAG_NAND, "Set [paths] nand_root in Config.toml.\n");
    std::string details = message ? message : "The configured NAND could not be initialized.";
    if (!path.empty()) {
        details += "\n\nPath: ";
        details += RuntimeConfigFile::PathToUtf8(path);
    }
    details += "\n\nSet [paths] nand_root in Config.toml and try again.";
    // Same fatal idiom as the DVD and OS paths: crash artifacts first so the run
    // folder always has them, then a non-zero exit code, the popup, and the
    // "already reported" latch so the atexit handler does not stack a second
    // generic report on top of this one.
    RuntimeCrash::WriteCrashArtifacts("nand_root", details);
    SetRuntimeExitCode(EXIT_FAILURE);
    ShowRuntimeFatalPopup("NAND initialization failed", details);
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

inline std::filesystem::path ResolveConfiguredPath(const std::string& value) {
    return RuntimeConfigFile::ResolveRelativeToConfig(value);
}

// NSMBW: which game folder this is, as a fingerprint of its files (relative path and size of each),
// so a mod of the game (New Super Mario Bros. Wii 2 - The Next Levels...) is told apart from the
// original. Empty when there is no readable game folder.
inline std::string GameSaveKey() {
    const std::filesystem::path root = RuntimeConfigFile::ResolvedDvdRoot();
    std::error_code ec;
    if (root.empty()) {
        return {};
    }
    std::vector<std::pair<std::string, uint64_t>> files;
    // A disc image has no fingerprint of its own: it uses the shared NAND, so switching from an
    // extracted folder to an image of the same disc keeps the saves.
    if (!std::filesystem::is_directory(root, ec) || ec) {
        return {};
    }
    for (std::filesystem::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileError;
        if (it->is_regular_file(fileError)) {
            const auto relative = std::filesystem::relative(it->path(), root, fileError).generic_u8string();
            files.emplace_back(std::string(relative.begin(), relative.end()), it->file_size(fileError));
        }
    }
    if (files.empty()) {
        return {};
    }
    std::sort(files.begin(), files.end());
    uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](const void* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            hash = (hash ^ static_cast<const uint8_t*>(data)[i]) * 1099511628211ull;
        }
    };
    for (const auto& [name, size] : files) {
        mix(name.data(), name.size());
        mix(&size, sizeof(size));
    }
    char text[17];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

// The saves: NAND in the user data folder belongs to the first game played with it (its key in
// NAND/.game); every other game gets Saves/<key>/NAND, so the original and a mod don't share files.
inline std::filesystem::path ManagedNandRootPath() {
    static const std::filesystem::path path = [] {
        const std::filesystem::path shared = RuntimeConfigFile::ApplicationDataDirectory() / "NAND";
        const std::string key = GameSaveKey();
        if (key.empty()) {
            return shared;
        }
        const std::filesystem::path owner = shared / ".game";
        std::string ownerKey;
        {
            std::ifstream in(owner);
            std::getline(in, ownerKey);
            // Written on Windows the line ends "\r\n": a save folder copied to Linux must still match.
            while (!ownerKey.empty() && (ownerKey.back() == '\r' || ownerKey.back() == ' ')) {
                ownerKey.pop_back();
            }
        }
        if (ownerKey.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(shared, ec);
            std::ofstream out(owner, std::ios::trunc);
            out << key << "\n" << RuntimeConfigFile::PathToUtf8(RuntimeConfigFile::ResolvedDvdRoot()) << "\n";
            return shared;
        }
        if (ownerKey == key) {
            return shared;
        }
        const std::filesystem::path own = RuntimeConfigFile::ApplicationDataDirectory() / "Saves" / key / "NAND";
        RT_LOGF(RT_TAG_NAND, "this game's saves: %s\n", RuntimeConfigFile::PathToUtf8(own).c_str());
        return own;
    }();
    return path;
}

inline std::optional<std::filesystem::path> BootstrapPayloadPath() {
    if (auto executableDirectory = RuntimeConfigFile::ExecutableDirectory()) {
        const auto adjacent = *executableDirectory / "wii_bootstrap";
        if (ExistingDirectory(adjacent / "shared2" / "wc24")) {
            return adjacent;
        }
    }

    // This makes developer-tree launches work without changing their release layout.
    for (auto base = std::filesystem::current_path(); !base.empty();) {
        const auto candidate = base / "runtime" / "assets" / "wii";
        if (ExistingDirectory(candidate / "shared2" / "wc24")) {
            return candidate;
        }
        const auto parent = base.parent_path();
        if (parent == base) {
            break;
        }
        base = parent;
    }
    return std::nullopt;
}

#if defined(MKW_PLATFORM_SWITCH)
#include <cerrno>
#include <cstdio>

// std::filesystem::copy_file misreports on Horizon, so Switch copies go
// through plain C I/O instead.
inline bool CopyFileBytes(const std::filesystem::path& source,
                          const std::filesystem::path& destination, std::error_code& ec) {
    std::FILE* in = std::fopen(source.c_str(), "rb");
    if (in == nullptr) {
        ec = std::error_code(errno, std::generic_category());
        return false;
    }
    std::FILE* out = std::fopen(destination.c_str(), "wb");
    if (out == nullptr) {
        ec = std::error_code(errno, std::generic_category());
        std::fclose(in);
        return false;
    }
    // Horizon's fread returns 0 at end of file without setting feof, so looping
    // on feof spins forever. Track the expected size and stop on a short read.
    std::fseek(in, 0, SEEK_END);
    const long expected = std::ftell(in);
    std::rewind(in);
    char buffer[4096];
    bool ok = true;
    long total = 0;
    while (true) {
        const size_t count = std::fread(buffer, 1, sizeof(buffer), in);
        if (count == 0) {
            ok = expected < 0 || total == expected;
            break;
        }
        if (std::fwrite(buffer, 1, count, out) != count) {
            ok = false;
            break;
        }
        total += static_cast<long>(count);
    }
    if (!ok) {
        RT_LOG(RT_TAG_NAND) << "short copy " << total << "/" << expected << " "
                            << RuntimeConfigFile::PathToUtf8(source) << std::endl;
    }
    if (std::fclose(out) != 0) {
        ok = false;
    }
    std::fclose(in);
    if (!ok) {
        ec = std::error_code(errno != 0 ? errno : EIO, std::generic_category());
    } else {
        ec.clear();
    }
    return ok;
}
#endif

inline bool CopyBootstrapFile(const std::filesystem::path& sourceRoot,
                              const std::filesystem::path& destinationRoot,
                              const std::filesystem::path& relativePath,
                              std::error_code& ec) {
    const auto source = sourceRoot / relativePath;
    const auto destination = destinationRoot / relativePath;
    if (std::filesystem::exists(destination, ec)) {
        return !ec;
    }

    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        return false;
    }
#if defined(MKW_PLATFORM_SWITCH)
    // std::filesystem::copy_file is unreliable on Horizon, so copy manually.
    return CopyFileBytes(source, destination, ec);
#else
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, ec);
    return !ec;
#endif
}

// Create these WC24 files only for a new profile; never overwrite user data.
constexpr std::string_view kBootstrapFiles[] = {
    "shared2/wc24/misc.bin",
    "shared2/wc24/nwc24dl.bin",
    "shared2/wc24/nwc24fl.bin",
    "shared2/wc24/nwc24fls.bin",
    "shared2/wc24/nwc24msg.cbk",
    "shared2/wc24/nwc24msg.cfg",
    "shared2/wc24/mbox/Readme.txt",
    "shared2/wc24/mbox/wc24recv.ctl",
    "shared2/wc24/mbox/wc24recv.mbx",
    "shared2/wc24/mbox/wc24send.ctl",
    "shared2/wc24/mbox/wc24send.mbx",
};

// Add first-run WC24 files only when the NAND has none yet.
inline bool SeedMissingBootstrapFiles(const std::filesystem::path& root) {
    const auto payload = BootstrapPayloadPath();
    if (!payload) {
        return false;
    }
    std::error_code ec;
    for (const std::string_view file : kBootstrapFiles) {
        const std::filesystem::path relativePath{std::string(file)};
        ec.clear();
        if (!CopyBootstrapFile(*payload, root, relativePath, ec)) {
            RT_LOG(RT_TAG_NAND) << "could not create "
                                << RuntimeConfigFile::PathToUtf8(root / relativePath) << " ("
                                << ec.message() << ")" << std::endl;
            return false;
        }
        RT_LOG(RT_TAG_NAND) << "seeded " << file << std::endl;
    }
    return true;
}

// Copies files that are missing under `to` from `from`, creating directories. Used to give a game's
// own NAND the saves an earlier disc-image run left in the shared one.
inline void CopyTreeMissing(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    if (!std::filesystem::is_directory(from, ec)) {
        RT_LOGF(RT_TAG_NAND, "no saves to copy: %s isn't a folder\n", RuntimeConfigFile::PathToUtf8(from).c_str());
        return;
    }
    // lexically_relative() only compares the path text; std::filesystem::relative() canonicalises,
    // which fails on Horizon and silently skipped every file of the seed.
    size_t copied = 0, seen = 0;
    for (std::filesystem::recursive_directory_iterator it(from, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileError;
        if (!it->is_regular_file(fileError)) {
            continue;
        }
        const std::filesystem::path relative = it->path().lexically_relative(from);
        if (relative.empty()) {
            continue;
        }
        ++seen;
        std::error_code copyError;
        CopyBootstrapFile(from, to, relative, copyError);
        if (copyError) {
            RT_LOGF(RT_TAG_NAND, "could not copy %s: %s\n",
                    RuntimeConfigFile::PathToUtf8(to / relative).c_str(), copyError.message().c_str());
        } else {
            ++copied;
        }
    }
    if (ec) {
        RT_LOGF(RT_TAG_NAND, "stopped listing %s: %s\n", RuntimeConfigFile::PathToUtf8(from).c_str(),
                ec.message().c_str());
    }
    RT_LOGF(RT_TAG_NAND, "copied %zu of %zu save file(s) from %s\n", copied, seen,
            RuntimeConfigFile::PathToUtf8(from).c_str());
}

inline std::filesystem::path CreateManagedNandRoot() {
    const std::filesystem::path root = ManagedNandRootPath();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec || !std::filesystem::is_directory(root, ec)) {
        FailNandRoot("Unable to create managed NAND root", root);
    }

    // A game that gets a NAND of its own (an extracted folder after the saves were made from a disc
    // image, which uses the shared NAND) starts with what the shared NAND holds: the player's progress
    // carries over instead of the folder looking like a fresh install.
    const std::filesystem::path shared = RuntimeConfigFile::ApplicationDataDirectory() / "NAND";
    if (root != shared) {
        std::error_code titleError;
        if (!std::filesystem::exists(root / "title", titleError) &&
            std::filesystem::is_directory(shared / "title", titleError)) {
            CopyTreeMissing(shared / "title", root / "title");
        }
    }

    if (!SeedMissingBootstrapFiles(root)) {
        FailNandRoot("Unable to initialize managed NAND", root);
    }

    const auto marker = root / ".mkw_recompiled_managed_nand";
    ec.clear();
    if (!std::filesystem::exists(marker, ec)) {
        std::ofstream markerFile(marker, std::ios::trunc);
        if (!markerFile) {
            FailNandRoot("Managed NAND root is not writable", root);
        }
        markerFile << "version=1\n";
        markerFile.close();
        if (!markerFile) {
            FailNandRoot("Unable to finish managed NAND initialization", root);
        }
    }

    RT_LOG(RT_TAG_NAND) << "using managed NAND root: " << RuntimeConfigFile::PathToUtf8(root)
                        << std::endl;
    return root;
}

// Network play boots every device from a NAND of its own (netplay_start.h): the host's save on top
// of the runtime's standard seeds, so all of them read the same bytes.
inline std::filesystem::path& NandRootOverride() {
    static std::filesystem::path path;
    return path;
}

inline std::filesystem::path ResolveNandRootPath() {
    if (const auto& override = NandRootOverride(); !override.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(override, ec);
        if (!SeedMissingBootstrapFiles(override)) {
            RT_LOG(RT_TAG_NAND) << "first-run WC24 seeding failed for the network play NAND" << std::endl;
        }
        RT_LOG(RT_TAG_NAND) << "using the network play NAND: " << RuntimeConfigFile::PathToUtf8(override) << std::endl;
        return override;
    }
    const std::string configPath = RuntimeConfigFile::NandRoot();
    if (!configPath.empty()) {
        const auto path = ResolveConfiguredPath(configPath);
        if (auto existing = ExistingDirectory(path)) {
            // Seed only a new configured NAND so existing frontend data stays unchanged.
            if (!SeedMissingBootstrapFiles(*existing)) {
                RT_LOG(RT_TAG_NAND) << "first-run WC24 seeding failed for the configured NAND root" << std::endl;
            }
            return *existing;
        }
        FailNandRoot("Configured NAND root is not an existing directory", path);
    }
    return CreateManagedNandRoot();
}

inline std::filesystem::path DiscoverNandRootPath() {
    static const auto root = [] {
        const auto resolved = ResolveNandRootPath();
        std::string error;
        if (!RuntimeNandSettings::Ensure(resolved, error)) {
            FailNandRoot(error.c_str(), RuntimeNandSettings::FilePath(resolved));
        }
        return resolved;
    }();
    return root;
}

} // namespace RuntimeNandPath
