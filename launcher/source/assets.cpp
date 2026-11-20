#include "assets.hpp"

#include "http.hpp"
#include "json_util.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "sha.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

namespace launcher {
namespace {
namespace fs = std::filesystem;

constexpr size_t kMaxCatalogSize = 4 * 1024 * 1024;
constexpr size_t kMaxBannerSize = 8 * 1024 * 1024;

std::mutex g_fetch_mutex;

auto Lowercase(std::string value) -> std::string {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

auto ReadFile(const fs::path& path, std::vector<u8>& data) -> bool {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !data.empty();
}

auto WriteFile(const fs::path& path, const std::vector<u8>& data) -> bool {
    const std::string target = path.string();
    const std::string temp = target + ".tmp";
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    const bool wrote = static_cast<bool>(file);
    file.close();
    if (!wrote || !file) {
        std::remove(temp.c_str());
        return false;
    }

    std::remove(target.c_str());
    if (std::rename(temp.c_str(), target.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

auto ReadComponent(yyjson_val* object, const char* key) -> Component {
    Component component;
    yyjson_val* node = yyjson_obj_get(object, key);
    if (node == nullptr || !yyjson_is_obj(node)) {
        return component;
    }

    auto get_string = [node](const char* field) -> std::string {
        yyjson_val* value = yyjson_obj_get(node, field);
        return yyjson_is_str(value) ? yyjson_get_str(value) : std::string{};
    };

    component.recomp_version = get_string("recomp_version");
    component.game_version = get_string("game_version");
    component.region = get_string("region");
    const std::string legacy_version = get_string("version");
    if (component.recomp_version.empty()) {
        component.recomp_version = legacy_version;
    }
    if (component.game_version.empty()) {
        component.game_version = legacy_version;
    }
    component.url = get_string("url");
    component.path = get_string("path");
    component.sha256 = get_string("sha256");
    component.image_sha256 = get_string("image_sha256");
    yyjson_val* size = yyjson_obj_get(node, "size");
    component.size = yyjson_is_num(size) ? static_cast<s64>(yyjson_get_num(size)) : 0;
    yyjson_val* image_size = yyjson_obj_get(node, "image_size");
    component.image_size = yyjson_is_num(image_size) ? static_cast<s64>(yyjson_get_num(image_size)) : 0;
    return component;
}

auto ReadBanner(yyjson_val* object, const char* key) -> Banner {
    Banner banner;
    yyjson_val* node = yyjson_obj_get(object, key);
    if (node == nullptr || !yyjson_is_obj(node)) {
        return banner;
    }

    auto get_string = [node](const char* field) -> std::string {
        yyjson_val* value = yyjson_obj_get(node, field);
        return yyjson_is_str(value) ? yyjson_get_str(value) : std::string{};
    };

    banner.url = get_string("url");
    banner.path = get_string("path");
    banner.sha256 = get_string("sha256");
    yyjson_val* size = yyjson_obj_get(node, "size");
    banner.size = yyjson_is_num(size) ? static_cast<s64>(yyjson_get_num(size)) : 0;
    return banner;
}

auto ParseAssets(const std::vector<u8>& data, Assets& out) -> Result {
    yyjson_doc* doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), 0);
    if (doc == nullptr) {
        log_write("[assets] invalid json\n");
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    Assets parsed;

    if (yyjson_is_obj(root)) {
        yyjson_val* recomp_version = yyjson_obj_get(root, "recomp_version");
        parsed.recomp_version = yyjson_is_str(recomp_version) ? yyjson_get_str(recomp_version) : "";

        yyjson_val* launcher = yyjson_obj_get(root, "launcher");
        if (yyjson_is_obj(launcher)) {
            yyjson_val* version = yyjson_obj_get(launcher, "version");
            yyjson_val* url_value = yyjson_obj_get(launcher, "url");
            yyjson_val* sha = yyjson_obj_get(launcher, "sha256");
            yyjson_val* size = yyjson_obj_get(launcher, "size");
            parsed.launcher_version = yyjson_is_str(version) ? yyjson_get_str(version) : "";
            parsed.launcher_url = yyjson_is_str(url_value) ? yyjson_get_str(url_value) : "";
            parsed.launcher_sha256 = yyjson_is_str(sha) ? yyjson_get_str(sha) : "";
            parsed.launcher_size = yyjson_is_num(size) ? static_cast<s64>(yyjson_get_num(size)) : 0;
        }

        yyjson_val* games = yyjson_obj_get(root, "games");
        if (yyjson_is_obj(games)) {
            yyjson_val* key = nullptr;
            yyjson_val* value = nullptr;
            yyjson_obj_iter iter;
            yyjson_obj_iter_init(games, &iter);
            while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
                value = yyjson_obj_iter_get_val(key);
                if (!yyjson_is_obj(value)) {
                    continue;
                }

                AssetsGame game;
                game.id = yyjson_get_str(key);
                if (!IsSafeId(game.id)) {
                    log_write("[assets] skipping unsafe game id %s\n", game.id.c_str());
                    continue;
                }
                yyjson_val* name = yyjson_obj_get(value, "name");
                game.name = yyjson_is_str(name) ? yyjson_get_str(name) : game.id;
                yyjson_val* author = yyjson_obj_get(value, "author");
                game.author = yyjson_is_str(author) ? yyjson_get_str(author) : "";
                yyjson_val* components = yyjson_obj_get(value, "components");
                if (yyjson_is_obj(components)) {
                    game.nro = ReadComponent(components, "nro");
                    game.romfs = ReadComponent(components, "romfs");
                    // A RomFS is a pack, not a recompiled NRO; keep the legacy
                    // version fallback only for its game version.
                    game.romfs.recomp_version.clear();
                }
                game.banner = ReadBanner(value, "banner");
                // Every installable game needs a recompiled NRO. A data-only entry cannot be launched.
                if (game.nro.Valid() && !game.nro.recomp_version.empty()) {
                    parsed.games[game.id] = std::move(game);
                } else {
                    log_write("[assets] skipping %s: no usable nro component\n", game.id.c_str());
                }
            }
        }
    }

    yyjson_doc_free(doc);

    if (parsed.games.empty() && parsed.launcher_url.empty()) {
        log_write("[assets] no usable entries\n");
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    parsed.valid = true;
    out = std::move(parsed);
    log_write("[assets] %zu game(s), recomp %s, launcher %s\n", out.games.size(),
              out.recomp_version.c_str(), out.launcher_version.c_str());
    return 0;
}

void FetchBanner(const std::string& id, const Banner& banner, const std::function<bool()>& canceled) {
    if (banner.url.empty() || !IsSafeId(id)) {
        return;
    }

    const auto directory = fs::path(BannersDirectory());
    std::error_code ec;
    fs::create_directories(directory, ec);
    const auto path = (directory / (id + ".png")).string();

    if (fs::is_regular_file(path, ec)) {
        if (banner.sha256.empty()) {
            return;
        }
        std::string actual;
        if (R_SUCCEEDED(FileSha256(path, actual)) && Lowercase(actual) == Lowercase(banner.sha256)) {
            return;
        }
    }

    std::vector<u8> data;
    if (R_FAILED(http::Get(banner.url, data, canceled, kMaxBannerSize))) {
        log_write("[assets] banner %s unavailable\n", id.c_str());
        return;
    }

    const std::string part = path + ".part";
    FILE* file = std::fopen(part.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    const bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    const bool closed = std::fclose(file) == 0;
    if (!written || !closed) {
        fs::remove(part, ec);
        return;
    }

    if (!banner.sha256.empty()) {
        std::string actual;
        if (R_FAILED(FileSha256(part, actual)) || Lowercase(actual) != Lowercase(banner.sha256)) {
            log_write("[assets] banner %s checksum mismatch\n", id.c_str());
            fs::remove(part, ec);
            return;
        }
    }

    fs::remove(path, ec);
    fs::rename(part, path, ec);
    if (ec) {
        fs::remove(part, ec);
    }
}

} // namespace

auto AssetsUrl() -> std::string {
    std::ifstream file((fs::path(RootDirectory()) / "assets.url").string());
    if (file) {
        std::string url;
        std::getline(file, url);
        url = Trim(url);
        if (!url.empty()) {
            return url;
        }
    }
    return ASSETS_URL;
}

auto LoadLocalAssets(Assets& out) -> Result {
    const auto base = fs::path(RootDirectory());
    std::vector<u8> data;

    if (ReadFile(base / "assets.local.json", data)) {
        log_write("[assets] using assets.local.json\n");
        if (R_SUCCEEDED(ParseAssets(data, out))) {
            return 0;
        }
    }

    if (ReadFile(base / "assets.cache.json", data)) {
        log_write("[assets] using cached assets.json\n");
        if (R_SUCCEEDED(ParseAssets(data, out))) {
            return 0;
        }
    }

    if (ReadFile(base / "assets.json", data)) {
        log_write("[assets] using bundled assets.json\n");
        return ParseAssets(data, out);
    }

    log_write("[assets] no local catalogue\n");
    return MAKERESULT(Module_Libnx, LibnxError_NotFound);
}

Result FetchRemoteAssets(Assets& out, const std::function<bool()>& canceled) {
    // A silent startup refresh can overlap a user-initiated fetch. Keep the cache and banner
    // writes serialized, but do not block shutdown or cancellation on the other fetch.
    std::unique_lock<std::mutex> fetch_lock(g_fetch_mutex, std::defer_lock);
    while (!fetch_lock.try_lock()) {
        if (canceled && canceled()) {
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const auto base = fs::path(RootDirectory());
    std::vector<u8> data;

    const auto url = AssetsUrl();
    log_write("[assets] fetching %s\n", url.c_str());
    Result rc = http::Get(url, data, canceled, kMaxCatalogSize);
    if (R_FAILED(rc)) {
        log_write("[assets] fetch failed 0x%08X\n", static_cast<unsigned>(rc));
        return rc;
    }

    Assets parsed;
    rc = ParseAssets(data, parsed);
    if (R_FAILED(rc)) {
        return rc;
    }
    if (parsed.games.empty()) {
        log_write("[assets] catalogue has no games\n");
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    // Keep the last good catalog so the next offline launch still lists mods.
    if (!WriteFile(base / "assets.cache.json", data)) {
        log_write("[assets] could not write assets.cache.json\n");
    }

    for (const auto& [id, game] : parsed.games) {
        if (game.banner.Valid()) {
            FetchBanner(id, game.banner, canceled);
        }
    }

    out = std::move(parsed);
    return 0;
}

} // namespace launcher
