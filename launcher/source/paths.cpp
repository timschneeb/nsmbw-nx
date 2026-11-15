#include "paths.hpp"

namespace launcher {
namespace {
const std::string g_root = "sdmc:/switch/nsmbw";
const std::string g_launcher = g_root + "/launcher.nro";
const std::string g_games = g_root + "/games";
const std::string g_version = LAUNCHER_VERSION;
} // namespace

const std::string& RootDirectory() { return g_root; }
const std::string& LauncherPath() { return g_launcher; }
const std::string& GamesDirectory() { return g_games; }
const std::string& LauncherVersion() { return g_version; }

} // namespace launcher
