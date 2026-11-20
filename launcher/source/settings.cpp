#include "settings.hpp"

#include "paths.hpp"

#include <yyjson.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace launcher {
namespace {

namespace fs = std::filesystem;

auto StyleFromName(const std::string& name) -> DisplayStyle {
    if (name == "banner_grid") {
        return DisplayStyle::BannerGrid;
    }
    if (name == "grid_detail") {
        return DisplayStyle::GridDetail;
    }
    return DisplayStyle::Detail;
}

auto StyleToName(DisplayStyle style) -> const char* {
    switch (style) {
        case DisplayStyle::BannerGrid:
            return "banner_grid";
        case DisplayStyle::GridDetail:
            return "grid_detail";
        case DisplayStyle::Detail:
        default:
            return "detail";
    }
}

} // namespace

auto ReadSettings() -> LauncherSettings {
    LauncherSettings settings;

    std::ifstream file(fs::path(RootDirectory()) / "settings.json");
    if (!file) {
        return settings;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const auto text = buffer.str();

    yyjson_doc* doc = yyjson_read(text.c_str(), text.size(), 0);
    if (doc == nullptr) {
        return settings;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        if (yyjson_val* style = yyjson_obj_get(root, "display_style"); yyjson_is_str(style)) {
            settings.display_style = StyleFromName(yyjson_get_str(style));
        }
        if (yyjson_val* show = yyjson_obj_get(root, "show_download_row"); yyjson_is_bool(show)) {
            settings.show_download_row = yyjson_get_bool(show);
        }
    }

    yyjson_doc_free(doc);
    return settings;
}

void WriteSettings(const LauncherSettings& settings) {
    const std::string target = (fs::path(RootDirectory()) / "settings.json").string();
    const std::string temp = target + ".tmp";
    std::ofstream file(temp, std::ios::trunc);
    if (!file) {
        return;
    }

    file << "{\n";
    file << "  \"display_style\": \"" << StyleToName(settings.display_style) << "\",\n";
    file << "  \"show_download_row\": " << (settings.show_download_row ? "true" : "false") << "\n";
    file << "}\n";
    file.close();
    if (!file) {
        std::remove(temp.c_str());
        return;
    }

    std::remove(target.c_str());
    if (std::rename(temp.c_str(), target.c_str()) != 0) {
        std::remove(temp.c_str());
    }
}

auto DisplayStyleName(DisplayStyle style) -> const char* {
    switch (style) {
        case DisplayStyle::BannerGrid:
            return "Banner Grid";
        case DisplayStyle::GridDetail:
            return "Detail Grid";
        case DisplayStyle::Detail:
        default:
            return "Detail";
    }
}

} // namespace launcher
