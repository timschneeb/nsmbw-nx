#include "games.hpp"

#include "log.hpp"
#include "paths.hpp"

#include <yyjson.h>

#include <algorithm>
#include <filesystem>
#include <switch/runtime/devices/fs_dev.h>
#include <fstream>
#include <map>
#include <sstream>
#include <cstdio>
#include <cstring>

namespace launcher {
namespace {
namespace fs = std::filesystem;

auto Escape(const std::string& value) -> std::string {
    std::string out;
    for (char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

auto IsDirectory(const fs::path& path) -> bool {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

auto IsFile(const fs::path& path) -> bool {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

auto DefaultName(const std::string& id) -> std::string {
    if (id == "vanilla") {
        return "New Super Mario Bros. Wii";
    }
    return id;
}
} // namespace

auto ReadInstalled(const std::string& dir) -> Installed {
    Installed installed;

    std::ifstream file(fs::path(dir) / "installed.json");
    if (!file) {
        return installed;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const auto text = buffer.str();

    yyjson_doc* doc = yyjson_read(text.c_str(), text.size(), 0);
    if (doc == nullptr) {
        log_write("[games] invalid installed.json in %s\n", dir.c_str());
        return installed;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        yyjson_val* name = yyjson_obj_get(root, "name");
        if (yyjson_is_str(name)) {
            installed.name = yyjson_get_str(name);
        }

        yyjson_val* components = yyjson_obj_get(root, "components");
        if (yyjson_is_obj(components)) {
            yyjson_val* nro = yyjson_obj_get(components, "nro");
            yyjson_val* romfs = yyjson_obj_get(components, "romfs");
            installed.nro_version = yyjson_is_str(nro) ? yyjson_get_str(nro) : "";
            installed.romfs_version = yyjson_is_str(romfs) ? yyjson_get_str(romfs) : "";
        }
        installed.present = true;
    }

    yyjson_doc_free(doc);
    return installed;
}

Result WriteInstalled(const std::string& dir, const Installed& installed) {
    std::error_code ec;
    fs::create_directories(dir, ec);

    std::ofstream file(fs::path(dir) / "installed.json", std::ios::trunc);
    if (!file) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    file << "{\n";
    file << "  \"name\": \"" << Escape(installed.name) << "\",\n";
    file << "  \"components\": {\n";
    file << "    \"nro\": \"" << Escape(installed.nro_version) << "\",\n";
    file << "    \"romfs\": \"" << Escape(installed.romfs_version) << "\"\n";
    file << "  }\n";
    file << "}\n";
    return 0;
}

auto ScanGames(const Assets& assets) -> std::vector<GameEntry> {
    std::vector<GameEntry> entries;
    std::map<std::string, size_t> index;

    const fs::path games_dir = GamesDirectory();
    std::error_code ec;
    fs::create_directories(games_dir, ec);

    for (fs::directory_iterator it(games_dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) {
            continue;
        }

        const std::string id = it->path().filename().string();
        if (id.empty() || id.front() == '.' || id.ends_with(".removed")) {
            continue;
        }
        const Installed installed = ReadInstalled(it->path().string());

        GameEntry entry;
        entry.id = id;
        entry.dir = it->path().string();
        entry.name = installed.name.empty() ? DefaultName(id) : installed.name;
        entry.nro_path = (it->path() / "game.nro").string();
        entry.data_path = (it->path() / "data").string();
        entry.installed = IsFile(entry.nro_path);
        entry.manual = !installed.present;
        entry.version = installed.nro_version;

        const auto catalog = assets.games.find(id);
        if (catalog != assets.games.end()) {
            entry.catalogued = true;
            if (!catalog->second.name.empty()) {
                entry.name = catalog->second.name;
            }
            if (!entry.installed) {
                entry.state = GameState::Install;
            } else if (entry.manual) {
                // a folder without installed.json is managed once it is updated.
                entry.state = GameState::Update;
            } else {
                const bool nro_stale = catalog->second.nro.Valid() &&
                                       CompareVersion(catalog->second.nro.version, installed.nro_version) != 0;
                const bool romfs_stale = catalog->second.romfs.Valid() &&
                                         CompareVersion(catalog->second.romfs.version,
                                                        installed.romfs_version) != 0;
                entry.state = (nro_stale || romfs_stale) ? GameState::Update : GameState::Launch;
                if (entry.state == GameState::Launch) {
                    entry.version = installed.nro_version;
                }
            }
        }

        if (entry.version.empty()) {
            entry.version = entry.manual ? "Manual install" : "Base game";
        }

        log_write("[games] %s state=%d installed=%d manual=%d nro=%s res=%s\n", id.c_str(),
                  static_cast<int>(entry.state), entry.installed, entry.manual,
                  installed.nro_version.c_str(), installed.romfs_version.c_str());
        index[id] = entries.size();
        entries.push_back(std::move(entry));
    }

    for (const auto& [id, catalog] : assets.games) {
        if (index.contains(id)) {
            continue;
        }

        GameEntry entry;
        entry.id = id;
        entry.name = catalog.name.empty() ? id : catalog.name;
        entry.dir = (games_dir / id).string();
        entry.nro_path = (games_dir / id / "game.nro").string();
        entry.data_path = (games_dir / id / "data").string();
        entry.state = GameState::Install;
        entry.version = "Not installed";
        entry.catalogued = true;
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const GameEntry& a, const GameEntry& b) {
        return a.name < b.name;
    });

    return entries;
}

Result RemoveGame(const std::string& dir) {
    // Horizon refuses to delete some directories recursively, so move the game out of the
    // scanned folder first and delete afterwards. A failed delete leaves a hidden .removed
    // folder instead of a half deleted game.
    std::error_code ec;
    const std::string trash = dir + ".removed";
    fsdevDeleteDirectoryRecursively(trash.c_str());
    std::filesystem::rename(dir, trash, ec);
    if (ec) {
        log_write("[games] remove failed %s: %s\n", dir.c_str(), ec.message().c_str());
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    const Result rc = fsdevDeleteDirectoryRecursively(trash.c_str());
    if (R_FAILED(rc)) {
        log_write("[games] leftover trash %s: 0x%08X\n", trash.c_str(), static_cast<unsigned>(rc));
    }
    return 0;
}

auto IsValidNro(const std::string& path) -> bool {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }

    // elf2nro prepends the Homebrew ABI header, so NRO0 sits at 0x10.
    char header[0x14]{};
    const size_t read = std::fread(header, 1, sizeof(header), file);
    std::fclose(file);
    if (read < sizeof(header)) {
        return false;
    }
    return std::memcmp(header, "NRO0", 4) == 0 || std::memcmp(header + 0x10, "NRO0", 4) == 0;
}

} // namespace launcher
