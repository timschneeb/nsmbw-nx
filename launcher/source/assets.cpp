#include "assets.hpp"

#include "http.hpp"
#include "log.hpp"
#include "paths.hpp"

#include <yyjson.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace launcher {
namespace {

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

    component.version = get_string("version");
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

} // namespace

auto CompareVersion(const std::string& a, const std::string& b) -> int {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() || j < b.size()) {
        long left = 0;
        long right = 0;
        while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]))) {
            left = left * 10 + (a[i] - '0');
            i++;
        }
        while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j]))) {
            right = right * 10 + (b[j] - '0');
            j++;
        }
        if (left != right) {
            return left < right ? -1 : 1;
        }
        while (i < a.size() && !std::isdigit(static_cast<unsigned char>(a[i]))) {
            i++;
        }
        while (j < b.size() && !std::isdigit(static_cast<unsigned char>(b[j]))) {
            j++;
        }
    }
    return 0;
}

auto AssetsUrl() -> std::string {
    std::ifstream file((std::filesystem::path(RootDirectory()) / "assets.url").string());
    if (file) {
        std::string url;
        std::getline(file, url);
        if (!url.empty()) {
            return url;
        }
    }
    return ASSETS_URL;
}

Result FetchAssets(Assets& out, const std::atomic_bool* cancel) {
    std::vector<u8> data;

    const auto base = std::filesystem::path(RootDirectory());
    std::ifstream override_file((base / "assets.local.json").string());
    if (override_file) {
        log_write("[assets] reading assets.local.json\n");
        data.assign(std::istreambuf_iterator<char>(override_file), std::istreambuf_iterator<char>());
    } else {
        const auto url = AssetsUrl();
        log_write("[assets] fetching %s\n", url.c_str());
        Result rc = http::Get(url, data, cancel);
        if (R_FAILED(rc)) {
            // Fall back to the catalog shipped with the launcher.
            std::ifstream bundled((base / "assets.json").string());
            if (!bundled) {
                return rc;
            }
            log_write("[assets] using bundled assets.json\n");
            data.assign(std::istreambuf_iterator<char>(bundled), std::istreambuf_iterator<char>());
        }
    }

    yyjson_doc* doc = yyjson_read(reinterpret_cast<const char*>(data.data()), data.size(), 0);
    if (doc == nullptr) {
        log_write("[assets] invalid json\n");
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    Assets parsed;

    if (yyjson_is_obj(root)) {
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
                yyjson_val* name = yyjson_obj_get(value, "name");
                game.name = yyjson_is_str(name) ? yyjson_get_str(name) : game.id;
                yyjson_val* components = yyjson_obj_get(value, "components");
                if (yyjson_is_obj(components)) {
                    game.nro = ReadComponent(components, "nro");
                    game.romfs = ReadComponent(components, "romfs");
                }
                if (game.nro.Valid() || game.romfs.Valid()) {
                    parsed.games[game.id] = std::move(game);
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
    log_write("[assets] %zu game(s), launcher %s\n", out.games.size(), out.launcher_version.c_str());
    return 0;
}

} // namespace launcher
