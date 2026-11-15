#include "assets.hpp"
#include "games.hpp"
#include "install.hpp"
#include "paths.hpp"
#include "sha.hpp"
#include "test_fixtures.hpp"
#include "ui/progress_box.hpp"

#include "switch.h"

#include <zstd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace launcher;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

auto find_entry(const std::vector<GameEntry>& entries, const std::string& id) -> const GameEntry* {
    const auto it = std::find_if(entries.begin(), entries.end(),
                                 [&id](const GameEntry& entry) { return entry.id == id; });
    return it == entries.end() ? nullptr : &*it;
}

auto install(const GameEntry& entry, const AssetsGame& game, std::string& error) -> Result {
    ui::ProgressBox pbox;
    return InstallGame(entry, game, pbox, error, false);
}

} // namespace

int main() {
    const fs::path root = "/tmp/opencode/launcher-pc-tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "games");
    fs::create_directories(root / "src");

    SetRootDirectory((root / "launcher.nro").string().c_str());

    const fs::path nro = root / "src" / "source.nro";
    test::WriteFakeNro(nro, "1.0.0");

    test::WriteCatalogue(root / "assets.json",
                         {test::GameJson("test", "Test Game", "Test Author", nro)}, "1.0.0");

    Assets assets;
    Result rc = LoadLocalAssets(assets);
    expect(R_SUCCEEDED(rc), "load local catalogue");
    expect(assets.games.contains("test"), "catalogue contains test game");
    expect(assets.games["test"].author == "Test Author", "author parsed");

    auto entries = ScanGames(assets);
    const GameEntry* entry = find_entry(entries, "test");
    expect(entry != nullptr, "test game listed");
    expect(entry != nullptr && entry->state == GameState::Install, "test game needs install");
    expect(entry != nullptr && entry->author == "Test Author", "author copied to entry");
    if (entry == nullptr) {
        return 1;
    }

    const GameEntry install_entry = *entry;
    std::string error;
    rc = install(install_entry, assets.games["test"], error);
    expect(R_SUCCEEDED(rc), "install test game: " + error);
    expect(fs::is_regular_file(install_entry.nro_path), "installed game.nro exists");
    expect(fs::is_regular_file(fs::path(install_entry.dir) / "installed.json"), "installed.json exists");

    entries = ScanGames(assets);
    entry = find_entry(entries, "test");
    expect(entry != nullptr && entry->state == GameState::Launch, "installed game is launchable");
    expect(entry != nullptr && entry->version == "1.0.0", "installed version");

    if (entry != nullptr) {
        ui::ProgressBox pbox;
        error.clear();
        rc = VerifyGame(*entry, pbox, error);
        expect(R_SUCCEEDED(rc), "verify installed game: " + error);
    }

    // Corrupting the NRO must be detected by the size check.
    {
        const auto path = fs::path(install_entry.nro_path);
        std::ofstream file(path, std::ios::binary | std::ios::app);
        file.put('\0');
        file.close();
        ui::ProgressBox pbox;
        error.clear();
        rc = VerifyGame(*find_entry(ScanGames(assets), "test"), pbox, error);
        expect(R_FAILED(rc), "corrupt game fails verification");
        fs::copy_file(nro, path, fs::copy_options::overwrite_existing);
    }

    // Delete game keeps data/.
    {
        fs::create_directories(fs::path(install_entry.dir) / "data");
        std::ofstream(fs::path(install_entry.dir) / "data" / "save.txt") << "save";
        rc = RemoveGame(install_entry.dir, install_entry.id, true);
        expect(R_SUCCEEDED(rc), "delete game");
        expect(!fs::exists(install_entry.nro_path), "delete game removes game.nro");
        expect(fs::is_regular_file(fs::path(install_entry.dir) / "data" / "save.txt"),
               "delete game keeps data");
        entries = ScanGames(assets);
        entry = find_entry(entries, "test");
        expect(entry != nullptr && entry->state == GameState::Install, "deleted game needs install");
    }

    // Reinstall, then delete game and save data.
    rc = install(install_entry, assets.games["test"], error);
    expect(R_SUCCEEDED(rc), "reinstall test game");
    rc = RemoveGame(install_entry.dir, install_entry.id, false);
    expect(R_SUCCEEDED(rc), "delete game and save data");
    expect(!fs::exists(install_entry.dir), "delete game removes the folder");

    // Bad checksum must not leave a game.nro.
    {
        AssetsGame bad = assets.games["test"];
        bad.id = "badsha";
        bad.nro.sha256 = std::string(64, '0');
        GameEntry bad_entry;
        bad_entry.id = "badsha";
        bad_entry.name = "Bad SHA";
        bad_entry.dir = (root / "games" / "badsha").string();
        bad_entry.nro_path = (fs::path(bad_entry.dir) / "game.nro").string();
        error.clear();
        rc = install(bad_entry, bad, error);
        expect(R_FAILED(rc), "bad sha is rejected");
        expect(!fs::exists(bad_entry.nro_path), "bad sha leaves no game.nro");
    }

    // Invalid NRO with a correct checksum must be rejected.
    {
        const fs::path bad_nro = root / "src" / "not-a-nro.bin";
        std::ofstream(bad_nro, std::ios::binary) << "this is not an nro";
        AssetsGame bad = assets.games["test"];
        bad.id = "badnro";
        bad.nro.path = bad_nro.string();
        bad.nro.sha256 = test::FileSha(bad_nro);
        bad.nro.size = test::FileSize(bad_nro);
        GameEntry bad_entry;
        bad_entry.id = "badnro";
        bad_entry.name = "Bad NRO";
        bad_entry.dir = (root / "games" / "badnro").string();
        bad_entry.nro_path = (fs::path(bad_entry.dir) / "game.nro").string();
        error.clear();
        rc = install(bad_entry, bad, error);
        expect(R_FAILED(rc), "invalid nro is rejected");
        expect(!fs::exists(bad_entry.nro_path), "invalid nro leaves no game.nro");
    }

    // URL install through the libcurl file protocol.
    {
        AssetsGame url_game = assets.games["test"];
        url_game.id = "urlgame";
        url_game.nro.path.clear();
        url_game.nro.url = "file://" + nro.string();
        GameEntry url_entry;
        url_entry.id = "urlgame";
        url_entry.name = "URL Game";
        url_entry.dir = (root / "games" / "urlgame").string();
        url_entry.nro_path = (fs::path(url_entry.dir) / "game.nro").string();
        error.clear();
        rc = install(url_entry, url_game, error);
        expect(R_SUCCEEDED(rc), "url install: " + error);
        expect(fs::is_regular_file(url_entry.nro_path), "url install writes game.nro");
    }

    // RomFS install.
    {
        const std::string raw = "romfs test payload";
        std::vector<u8> compressed(ZSTD_compressBound(raw.size()));
        const size_t compressed_size =
            ZSTD_compress(compressed.data(), compressed.size(), raw.data(), raw.size(), 3);
        compressed.resize(compressed_size);
        const fs::path romfs = root / "src" / "test.romfs.zst";
        {
            std::ofstream file(romfs, std::ios::binary);
            file.write(reinterpret_cast<const char*>(compressed.data()),
                       static_cast<std::streamsize>(compressed.size()));
        }

        AssetsGame game = assets.games["test"];
        game.id = "romfs";
        game.name = "RomFS Game";
        game.romfs.game_version = "1.0.0";
        game.romfs.region = "U";
        game.romfs.path = romfs.string();
        game.romfs.sha256 = test::FileSha(romfs);
        game.romfs.size = test::FileSize(romfs);
        game.romfs.image_size = static_cast<s64>(raw.size());
        {
            Sha256Context ctx;
            sha256ContextCreate(&ctx);
            sha256ContextUpdate(&ctx, raw.data(), raw.size());
            u8 hash[32];
            sha256ContextGetHash(&ctx, hash);
            static constexpr char digits[] = "0123456789abcdef";
            std::string image_sha;
            for (u8 byte : hash) {
                image_sha.push_back(digits[byte >> 4]);
                image_sha.push_back(digits[byte & 0xF]);
            }
            game.romfs.image_sha256 = image_sha;
        }

        GameEntry romfs_entry;
        romfs_entry.id = "romfs";
        romfs_entry.name = "RomFS Game";
        romfs_entry.dir = (root / "games" / "romfs").string();
        romfs_entry.nro_path = (fs::path(romfs_entry.dir) / "game.nro").string();
        romfs_entry.romfs_path = (fs::path(romfs_entry.dir) / "assets.romfs").string();
        error.clear();
        rc = install(romfs_entry, game, error);
        expect(R_SUCCEEDED(rc), "romfs install: " + error);
        expect(fs::is_regular_file(romfs_entry.romfs_path), "romfs image installed");
        expect(test::FileSize(romfs_entry.romfs_path) == static_cast<s64>(raw.size()), "romfs size");
        expect(test::FileSha(romfs_entry.romfs_path) == game.romfs.image_sha256, "romfs image hash");
    }

    // Remote catalogue through a file:// URL.
    {
        const fs::path remote = root / "remote-assets.json";
        test::WriteCatalogue(root / "assets.json",
                             {test::GameJson("test", "Test Game", "Test Author", nro)},
                             "1.0.0");
        fs::copy_file(root / "assets.json", remote, fs::copy_options::overwrite_existing);
        std::ofstream(root / "assets.url") << "file://" << remote.string() << "\n";
        fs::remove(root / "assets.cache.json", ec);

        Assets fetched;
        rc = FetchRemoteAssets(fetched, {});
        expect(R_SUCCEEDED(rc), "fetch remote catalogue");
        expect(fetched.games.contains("test"), "remote catalogue parsed");
        expect(fs::is_regular_file(root / "assets.cache.json"), "remote catalogue cached");
    }

    // Manual install version comes from the NRO NACP.
    {
        const fs::path manual = root / "games" / "manual";
        fs::create_directories(manual);
        test::WriteFakeNro(manual / "game.nro", "2.5.0");
        Assets empty;
        const auto all = ScanGames(empty);
        const auto* manual_entry = find_entry(all, "manual");
        expect(manual_entry != nullptr, "manual install listed");
        expect(manual_entry != nullptr && manual_entry->manual, "manual install flagged");
        expect(manual_entry != nullptr && manual_entry->version == "2.5.0",
               "manual install reads NACP version");
    }

    // An invalid NRO in a manual folder is flagged.
    {
        const fs::path broken = root / "games" / "broken";
        fs::create_directories(broken);
        std::ofstream(broken / "game.nro", std::ios::binary) << "broken";
        Assets empty;
        const auto all = ScanGames(empty);
        const auto* broken_entry = find_entry(all, "broken");
        expect(broken_entry != nullptr && !broken_entry->nro_valid, "invalid manual nro flagged");
    }

    // Legacy installed.json has no recomp_version/region. A managed install must migrate it.
    {
        const fs::path legacy_nro = root / "src" / "legacycat.nro";
        test::WriteFakeNro(legacy_nro, "0.0.1");
        const fs::path dir = root / "games" / "legacycat";
        fs::create_directories(dir);
        fs::copy_file(legacy_nro, dir / "game.nro", fs::copy_options::overwrite_existing);
        std::ofstream(dir / "installed.json")
            << "{\"name\":\"Legacy Cat\",\"components\":{\"nro\":{\"version\":\"0.0.1\","
               "\"sha256\":\"" << test::FileSha(dir / "game.nro") << "\",\"size\":"
            << test::FileSize(dir / "game.nro") << "}}}";

        fs::remove(root / "assets.cache.json", ec);
        test::WriteCatalogue(root / "assets.json",
                             {test::GameJson("test", "Test Game", "Test Author", nro),
                              test::GameJson("legacycat", "Legacy Cat", "Old Studio", legacy_nro,
                                             "0.0.1", "", "1.0.0")},
                             "0.0.1", "", {}, "0.0.1");
        Assets migrated;
        expect(R_SUCCEEDED(LoadLocalAssets(migrated)), "legacy catalogue loads");
        const auto all = ScanGames(migrated);
        const auto* legacy_entry = find_entry(all, "legacycat");
        expect(legacy_entry != nullptr && legacy_entry->state == GameState::Update,
               "legacy metadata marks the game as update");
        expect(legacy_entry != nullptr && legacy_entry->recomp_version.empty(),
               "legacy metadata has no recomp version");
        if (legacy_entry != nullptr) {
            const GameEntry entry_copy = *legacy_entry;
            error.clear();
            expect(R_SUCCEEDED(install(entry_copy, migrated.games["legacycat"], error)),
                   "legacy metadata update succeeds: " + error);
            const Installed installed = ReadInstalled(entry_copy.dir);
            expect(installed.nro.recomp_version == "0.0.1",
                   "legacy update records recomp version");
            expect(installed.nro.game_version == "1.0.0", "legacy update records game version");
            expect(installed.nro.region == "U", "legacy update records region");
        }
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "launcher PC tests passed\n";
    return 0;
}
