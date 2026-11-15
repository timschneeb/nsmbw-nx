#pragma once

#include <switch.h>

#include <atomic>

#include <map>
#include <string>

namespace launcher {

auto CompareVersion(const std::string& a, const std::string& b) -> int;

struct Component {
    std::string version;
    std::string url;
    std::string path;
    std::string sha256;
    s64 size{};
    // RomFS images carry the uncompressed image hash as well.
    std::string image_sha256;
    s64 image_size{};

    auto Valid() const -> bool { return !version.empty() && (!url.empty() || !path.empty()); }
};

struct AssetsGame {
    std::string id;
    std::string name;
    Component nro;
    Component romfs;
};

struct Assets {
    std::string launcher_version;
    std::string launcher_url;
    std::string launcher_sha256;
    s64 launcher_size{};
    std::map<std::string, AssetsGame> games;
    bool valid{};

    auto LauncherUpdate(const std::string& current) const -> bool {
        return !launcher_url.empty() && !launcher_version.empty() && CompareVersion(launcher_version, current) > 0;
    }
};

auto AssetsUrl() -> std::string;
Result FetchAssets(Assets& out, const std::atomic_bool* cancel = nullptr);

} // namespace launcher
