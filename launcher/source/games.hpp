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

struct InstalledComponent {
    std::string version;
    std::string sha256;
    s64 size{};
};

struct Installed {
    std::string name;
    InstalledComponent nro;
    InstalledComponent romfs;
    bool present{};
};

struct GameEntry {
    std::string id;
    std::string name;
    std::string author;
    std::string version;
    std::string dir;
    std::string nro_path;
    std::string romfs_path;
    GameState state{GameState::Launch};
    bool installed{};
    bool nro_valid{};
    bool manual{};
    bool base_ready{};
    u64 last_used{};
};

auto ReadInstalled(const std::string& dir) -> Installed;
Result WriteInstalled(const std::string& dir, const Installed& installed);
auto ScanGames(const Assets& assets) -> std::vector<GameEntry>;
auto IsValidNro(const std::string& path) -> bool;
// Reads the NRO's NACP display version, which is what a manual install reports.
auto ReadNroVersion(const std::string& path, std::string& version) -> Result;
// Removes the managed game files. With keep_data, saves, settings and disc images stay.
Result RemoveGame(const std::string& dir, const std::string& id, bool keep_data);
// Records a launch so the main list can sort by most recently used.
void TouchUsage(const std::string& id);
void RemoveUsage(const std::string& id);

} // namespace launcher
