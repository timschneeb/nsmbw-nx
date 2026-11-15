#pragma once

#include "assets.hpp"

#include <switch.h>

#include <string>
#include <vector>

namespace launcher {

enum class GameState {
    Launch,
    Update,
    Install,
};

struct Installed {
    std::string name;
    std::string nro_version;
    std::string romfs_version;
    bool present{};
};

struct GameEntry {
    std::string id;
    std::string name;
    std::string version;
    std::string dir;
    std::string nro_path;
    std::string data_path;
    GameState state{GameState::Launch};
    bool installed{};
    bool manual{};
    bool catalogued{};
};

auto ReadInstalled(const std::string& dir) -> Installed;
Result WriteInstalled(const std::string& dir, const Installed& installed);
auto ScanGames(const Assets& assets) -> std::vector<GameEntry>;
auto IsValidNro(const std::string& path) -> bool;
Result RemoveGame(const std::string& dir);

} // namespace launcher
