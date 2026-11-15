#include "games.hpp"

#include "json_util.hpp"
#include "log.hpp"
#include "paths.hpp"

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <switch/runtime/devices/fs_dev.h>

namespace launcher {
namespace {
namespace fs = std::filesystem;

auto IsFile(const fs::path& path) -> bool {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

auto FileSize(const fs::path& path) -> s64 {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    return ec ? -1 : static_cast<s64>(size);
}

auto DefaultName(const std::string& id) -> std::string {
    if (id == "vanilla") {
        return "New Super Mario Bros. Wii";
    }
    return id;
}

auto ReadUsageFile() -> std::map<std::string, u64> {
    std::map<std::string, u64> usage;

    std::ifstream file(fs::path(RootDirectory()) / "usage.json");
    if (!file) {
        return usage;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const auto text = buffer.str();

    yyjson_doc* doc = yyjson_read(text.c_str(), text.size(), 0);
    if (doc == nullptr) {
        return usage;
    }

    yyjson_val* root = yyjson_doc_get_root(doc);
    if (yyjson_is_obj(root)) {
        yyjson_obj_iter iter;
        yyjson_obj_iter_init(root, &iter);
        yyjson_val* key = nullptr;
        while ((key = yyjson_obj_iter_next(&iter)) != nullptr) {
            yyjson_val* value = yyjson_obj_iter_get_val(key);
            if (yyjson_is_str(key) && yyjson_is_num(value)) {
                usage[yyjson_get_str(key)] = static_cast<u64>(yyjson_get_num(value));
            }
        }
    }

    yyjson_doc_free(doc);
    return usage;
}

void WriteUsageFile(const std::map<std::string, u64>& usage) {
    const std::string target = (fs::path(RootDirectory()) / "usage.json").string();
    const std::string temp = target + ".tmp";
    std::ofstream file(temp, std::ios::trunc);
    if (!file) {
        return;
    }

    file << "{\n";
    size_t index = 0;
    for (const auto& [id, count] : usage) {
        file << "  \"" << JsonEscape(id) << "\": " << count;
        if (++index != usage.size()) {
            file << ",";
        }
        file << "\n";
    }
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

void ReadInstalledComponent(yyjson_val* node, InstalledComponent& out) {
    if (yyjson_is_str(node)) {
        out.version = yyjson_get_str(node);
        return;
    }
    if (!yyjson_is_obj(node)) {
        return;
    }

    yyjson_val* value = yyjson_obj_get(node, "version");
    if (yyjson_is_str(value)) {
        out.version = yyjson_get_str(value);
    }
    value = yyjson_obj_get(node, "sha256");
    if (yyjson_is_str(value)) {
        out.sha256 = yyjson_get_str(value);
    }
    value = yyjson_obj_get(node, "size");
    if (yyjson_is_num(value)) {
        out.size = static_cast<s64>(yyjson_get_num(value));
    }
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
            ReadInstalledComponent(yyjson_obj_get(components, "nro"), installed.nro);
            ReadInstalledComponent(yyjson_obj_get(components, "romfs"), installed.romfs);
        }
        installed.present = true;
    }

    yyjson_doc_free(doc);
    return installed;
}

Result WriteInstalled(const std::string& dir, const Installed& installed) {
    std::error_code ec;
    fs::create_directories(dir, ec);

    const fs::path path = fs::path(dir) / "installed.json";
    const std::string temp = path.string() + ".tmp";

    std::string text;
    text += "{\n";
    text += "  \"name\": \"" + JsonEscape(installed.name) + "\",\n";
    text += "  \"components\": {\n";
    text += "    \"nro\": { \"version\": \"" + JsonEscape(installed.nro.version) + "\", \"sha256\": \"" +
            JsonEscape(installed.nro.sha256) + "\", \"size\": " + std::to_string(installed.nro.size) + " },\n";
    text += "    \"romfs\": { \"version\": \"" + JsonEscape(installed.romfs.version) + "\", \"sha256\": \"" +
            JsonEscape(installed.romfs.sha256) + "\", \"size\": " + std::to_string(installed.romfs.size) + " }\n";
    text += "  }\n";
    text += "}\n";

    FILE* file = std::fopen(temp.c_str(), "wb");
    if (file == nullptr) {
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    const bool wrote = std::fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = std::fclose(file) == 0;
    if (!wrote || !closed) {
        std::remove(temp.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }

    std::remove(path.string().c_str());
    if (std::rename(temp.c_str(), path.string().c_str()) != 0) {
        std::remove(temp.c_str());
        return MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    return 0;
}

auto ScanGames(const Assets& assets) -> std::vector<GameEntry> {
    std::vector<GameEntry> entries;
    std::map<std::string, size_t> index;

    const fs::path games_dir = GamesDirectory();
    std::error_code ec;
    fs::create_directories(games_dir, ec);
    const auto usage = ReadUsageFile();

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
        entry.romfs_path = (it->path() / "assets.romfs").string();
        entry.installed = IsFile(entry.nro_path);
        entry.nro_valid = entry.installed && IsValidNro(entry.nro_path);
        entry.manual = !installed.present;
        entry.version = installed.nro.version;
        if (const auto used = usage.find(id); used != usage.end()) {
            entry.last_used = used->second;
        }

        if (entry.installed && entry.version.empty()) {
            std::string nro_version;
            if (R_SUCCEEDED(ReadNroVersion(entry.nro_path, nro_version))) {
                entry.version = std::move(nro_version);
            }
        }

        const auto catalog = assets.games.find(id);
        const bool catalogued = catalog != assets.games.end();
        if (catalogued) {
            if (!catalog->second.name.empty()) {
                entry.name = catalog->second.name;
            }
            entry.author = catalog->second.author;
            if (!entry.installed) {
                entry.state = GameState::Install;
            } else {
                const std::string installed_version =
                    entry.manual ? entry.version : installed.nro.version;
                bool nro_stale = !entry.nro_valid;
                if (catalog->second.nro.Valid() &&
                    CompareVersion(catalog->second.nro.version, installed_version) != 0) {
                    nro_stale = true;
                }
                // The catalogue size is authoritative for manual installs too: the release ZIP
                // ships game.nro without installed.json.
                if (catalog->second.nro.size > 0 &&
                    FileSize(entry.nro_path) != catalog->second.nro.size) {
                    nro_stale = true;
                }
                if (!entry.manual && installed.nro.size > 0 &&
                    FileSize(entry.nro_path) != installed.nro.size) {
                    nro_stale = true;
                }

                bool romfs_stale = false;
                if (catalog->second.romfs.Valid()) {
                    // A manual install has no recorded component versions. A correct image
                    // size is the best available proof that it is current.
                    if (entry.manual) {
                        romfs_stale = catalog->second.romfs.image_size > 0 &&
                                      FileSize(entry.romfs_path) != catalog->second.romfs.image_size;
                    } else {
                        romfs_stale =
                            CompareVersion(catalog->second.romfs.version, installed.romfs.version) != 0 ||
                            (installed.romfs.size > 0 &&
                             FileSize(entry.romfs_path) != installed.romfs.size);
                        if (catalog->second.romfs.image_size > 0 &&
                            FileSize(entry.romfs_path) != catalog->second.romfs.image_size) {
                            romfs_stale = true;
                        }
                    }
                }
                entry.state = (nro_stale || romfs_stale) ? GameState::Update : GameState::Launch;
                if (entry.state == GameState::Launch && !entry.manual) {
                    entry.version = installed.nro.version;
                }
            }
        }

        if (entry.version.empty()) {
            entry.version = entry.manual ? "Manual install" : "Base game";
        }

        // A folder left behind by Delete game can hold only saves. Do not list a game
        // that has no NRO and is not in the catalogue.
        if (!entry.installed && !catalogued) {
            log_write("[games] skipping %s: no game.nro and no catalogue entry\n", id.c_str());
            continue;
        }

        log_write("[games] %s state=%d installed=%d manual=%d nro=%s res=%s\n", id.c_str(),
                  static_cast<int>(entry.state), entry.installed, entry.manual,
                  installed.nro.version.c_str(), installed.romfs.version.c_str());
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
        entry.author = catalog.author;
        entry.dir = (games_dir / id).string();
        entry.nro_path = (games_dir / id / "game.nro").string();
        entry.romfs_path = (games_dir / id / "assets.romfs").string();
        entry.state = GameState::Install;
        entry.version = "Not installed";
        entries.push_back(std::move(entry));
    }

    std::sort(entries.begin(), entries.end(), [](const GameEntry& a, const GameEntry& b) {
        return a.name < b.name;
    });

    return entries;
}

Result RemoveGame(const std::string& dir, const std::string& id, bool keep_data) {
    if (keep_data) {
        static constexpr const char* kManagedFiles[] = {
            "game.nro",
            "game.nro.part",
            "assets.romfs",
            "assets.romfs.part",
            "installed.json",
            "installed.json.tmp",
        };

        const fs::path base = dir;
        bool failed = false;
        for (const char* name : kManagedFiles) {
            std::error_code ec;
            const fs::path path = base / name;
            if (fs::exists(path, ec) && !ec) {
                fs::remove(path, ec);
                if (ec) {
                    log_write("[games] could not delete %s: %s\n", path.c_str(), ec.message().c_str());
                    failed = true;
                }
            }
        }

        std::error_code exists_ec;
        if (!fs::exists(base, exists_ec)) {
            if (!failed) {
                RemoveUsage(id);
            }
            return failed ? MAKERESULT(Module_Libnx, LibnxError_IoError) : 0;
        }

        std::error_code empty_ec;
        if (!failed && fs::is_empty(base, empty_ec) && !empty_ec) {
            std::error_code remove_ec;
            fs::remove(base, remove_ec);
            if (remove_ec) {
                failed = true;
            }
        }

        if (!failed) {
            RemoveUsage(id);
        }
        return failed ? MAKERESULT(Module_Libnx, LibnxError_IoError) : 0;
    }

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

    RemoveUsage(id);

    const Result rc = fsdevDeleteDirectoryRecursively(trash.c_str());
    if (R_FAILED(rc)) {
        log_write("[games] leftover trash %s: 0x%08X\n", trash.c_str(), static_cast<unsigned>(rc));
    }
    return 0;
}

void TouchUsage(const std::string& id) {
    auto usage = ReadUsageFile();
    u64 next = 1;
    for (const auto& [key, value] : usage) {
        next = std::max(next, value + 1);
    }
    usage[id] = next;
    WriteUsageFile(usage);
}

void RemoveUsage(const std::string& id) {
    auto usage = ReadUsageFile();
    if (usage.erase(id) != 0) {
        WriteUsageFile(usage);
    }
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

Result ReadNroVersion(const std::string& path, std::string& version) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }

    u8 prefix[0x14]{};
    if (std::fread(prefix, 1, sizeof(prefix), file) != sizeof(prefix)) {
        std::fclose(file);
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    const long base = std::memcmp(prefix, "NRO0", 4) == 0
                          ? 0
                          : (std::memcmp(prefix + 0x10, "NRO0", 4) == 0 ? 0x10 : -1);
    if (base < 0) {
        std::fclose(file);
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    NroHeader header{};
    std::fseek(file, base, SEEK_SET);
    if (std::fread(&header, 1, sizeof(header), file) != sizeof(header) ||
        header.magic != NROHEADER_MAGIC) {
        std::fclose(file);
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }

    NroAssetHeader asset{};
    std::fseek(file, static_cast<long>(header.size), SEEK_SET);
    constexpr size_t kDisplayVersionEnd = offsetof(NacpStruct, display_version) + 0x10;
    if (std::fread(&asset, 1, sizeof(asset), file) != sizeof(asset) ||
        asset.magic != NROASSETHEADER_MAGIC ||
        asset.nacp.offset == 0 || asset.nacp.size < kDisplayVersionEnd) {
        std::fclose(file);
        return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    }

    NacpStruct nacp{};
    std::fseek(file, static_cast<long>(header.size + asset.nacp.offset), SEEK_SET);
    if (std::fread(&nacp, 1, kDisplayVersionEnd, file) != kDisplayVersionEnd) {
        std::fclose(file);
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    std::fclose(file);

    version.assign(nacp.display_version, strnlen(nacp.display_version, sizeof(nacp.display_version)));
    return 0;
}

} // namespace launcher
