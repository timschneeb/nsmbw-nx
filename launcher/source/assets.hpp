#pragma once

#include "version.hpp"

#include <switch.h>

#include <functional>
#include <map>
#include <string>

namespace launcher {

struct Component {
    // Recompiled NRO build version. Empty for a RomFS resource image.
    std::string recomp_version;
    // Game or mod-pack version: 1.0.0 for USA revision 1, 1.30 for Newer.
    std::string game_version;
    // Region the NRO or pack was made from: U for USA, E for Europe, J for Japan.
    std::string region;
    std::string url;
    std::string path;
    std::string sha256;
    s64 size{};
    // RomFS images carry the uncompressed image hash as well.
    std::string image_sha256;
    s64 image_size{};

    auto Valid() const -> bool {
        return !game_version.empty() && (!url.empty() || !path.empty());
    }
};

// Wide title image shown in the launcher row. Banners are not versioned
// components: they are small and can be replaced with the catalog.
struct Banner {
    std::string url;
    std::string path;
    std::string sha256;
    s64 size{};

    auto Valid() const -> bool { return !url.empty() || !path.empty(); }
};

struct AssetsGame {
    std::string id;
    std::string name;
    std::string author;
    Component nro;
    Component romfs;
    Banner banner;
};

struct Assets {
    std::string recomp_version;
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

// Loads assets.local.json, assets.cache.json or the bundled assets.json without network access.
Result LoadLocalAssets(Assets& out);

// Fetches the catalogue and its banners. The caller's catalogue stays untouched on failure.
Result FetchRemoteAssets(Assets& out, const std::function<bool()>& canceled = {});

} // namespace launcher
