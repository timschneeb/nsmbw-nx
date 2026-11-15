#include "paths.hpp"

#include <filesystem>

namespace launcher {
namespace {

std::string g_root = ".";
std::string g_launcher;
std::string g_games;
std::string g_banners;
std::string g_discs;
std::string g_base_disc;
const std::string g_version = LAUNCHER_VERSION;

void Rebuild() {
    g_launcher = g_root + "/launcher.nro";
    g_games = g_root + "/games";
    g_banners = g_root + "/banners";
    g_discs = g_root + "/discs";
    g_base_disc = g_discs + "/SMNE01";
}

struct Initializer {
    Initializer() {
        Rebuild();
    }
} g_initializer;

} // namespace

void SetRootDirectory(const std::string& argv0) {
    std::filesystem::path root;
    if (!argv0.empty()) {
        root = std::filesystem::path(argv0).parent_path();
    }
    if (root.empty()) {
        std::error_code ec;
        root = std::filesystem::current_path(ec);
        if (ec) {
            root = ".";
        }
    }

    g_root = root.generic_string();
    while (g_root.size() > 1 && g_root.back() == '/') {
        g_root.pop_back();
    }
    Rebuild();
}

const std::string& RootDirectory() { return g_root; }
const std::string& LauncherPath() { return g_launcher; }
const std::string& GamesDirectory() { return g_games; }
const std::string& BannersDirectory() { return g_banners; }
const std::string& DiscsDirectory() { return g_discs; }
const std::string& BaseDiscDirectory() { return g_base_disc; }
const std::string& LauncherVersion() { return g_version; }

} // namespace launcher
