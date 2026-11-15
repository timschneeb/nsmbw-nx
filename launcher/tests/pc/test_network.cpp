#include "assets.hpp"
#include "games.hpp"
#include "http.hpp"
#include "install.hpp"
#include "paths.hpp"
#include "sha.hpp"
#include "test_fixtures.hpp"
#include "ui/progress_box.hpp"

#include "switch.h"
#include "test_http_server.hpp"

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

auto get_result(const std::string& url, size_t max = 1 << 20) -> Result {
    std::vector<u8> data;
    return http::Get(url, data, {}, max);
}

auto download(const std::string& url, const fs::path& path) -> Result {
    return http::Download(url, path.string(), [](s64, s64) { return true; });
}

auto install(const GameEntry& entry, const AssetsGame& game, std::string& error) -> Result {
    ui::ProgressBox pbox;
    return InstallGame(entry, game, pbox, error, false);
}

auto make_zstd(const fs::path& path, const std::string& raw) -> void {
    std::vector<u8> compressed(ZSTD_compressBound(raw.size()));
    const size_t size = ZSTD_compress(compressed.data(), compressed.size(), raw.data(), raw.size(), 3);
    compressed.resize(size);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(compressed.data()),
               static_cast<std::streamsize>(compressed.size()));
}

} // namespace

int main() {
    const fs::path root = "/tmp/opencode/launcher-net-tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "src");
    SetRootDirectory((root / "launcher.nro").string().c_str());

    TestServer server;
    expect(server.Start(), "start test server");
    if (g_failures) {
        return 1;
    }

    server.Route("/ok", TestResponse{200, "hello"});
    server.Route("/missing", TestResponse{404, "not found"});
    server.Route("/error", TestResponse{500, "boom"});
    server.Route("/redirect", TestResponse{302, "", "text/plain", server.Url("/ok")});
    server.Route("/loop-a", TestResponse{302, "", "text/plain", server.Url("/loop-b")});
    server.Route("/loop-b", TestResponse{302, "", "text/plain", server.Url("/loop-a")});
    server.Route("/large", TestResponse{200, std::string(4096, 'x')});
    server.Route("/file", TestResponse{200, "network file payload"});
    server.Route("/partial", TestResponse{200, std::string(4096, 'p'), "application/octet-stream", "", true});
    server.Route("/chunked", TestResponse{200, "chunked network payload", "application/octet-stream", "", false, true});
    server.Route("/slow", TestResponse{200, std::string(256 * 1024, 's'), "application/octet-stream", "", false, false, 1, 4096});

    // http::Get basics.
    {
        std::vector<u8> data;
        expect(R_SUCCEEDED(http::Get(server.Url("/ok"), data, {}, 1024)), "get 200");
        expect(std::string(data.begin(), data.end()) == "hello", "get body");
    }
    expect(R_FAILED(get_result(server.Url("/missing"))), "get 404 fails");
    expect(R_FAILED(get_result(server.Url("/error"))), "get 500 fails");
    expect(R_FAILED(get_result(server.Url("/large"), 10)), "get max size fails");
    expect(get_result(server.Url("/redirect")) == 0, "get follows redirect");
    expect(R_FAILED(get_result(server.Url("/loop-a"))), "redirect loop fails");
    expect(R_FAILED(get_result("http://127.0.0.1:1/nope")), "unreachable host fails");

    // http::Download basics.
    {
        const auto path = root / "download.bin";
        expect(R_SUCCEEDED(download(server.Url("/file"), path)), "download 200");
        expect(test::ReadFile(path) == "network file payload", "download body");
        fs::remove(path);
    }
    {
        const auto path = root / "missing.bin";
        expect(R_FAILED(download(server.Url("/missing"), path)), "download 404 fails");
        expect(!fs::exists(path), "download 404 leaves no file");
    }
    {
        const auto path = root / "partial.bin";
        expect(R_FAILED(download(server.Url("/partial"), path)), "partial download fails");
        expect(!fs::exists(path), "partial download leaves no file");
    }
    {
        const auto path = root / "chunked.bin";
        expect(R_SUCCEEDED(download(server.Url("/chunked"), path)), "chunked download");
        expect(test::ReadFile(path) == "chunked network payload", "chunked body");
        fs::remove(path);
    }
    {
        const auto path = root / "cancelled.bin";
        int calls = 0;
        const Result rc = http::Download(server.Url("/slow"), path.string(), [&calls](s64, s64) {
            return ++calls < 2;
        });
        expect(R_FAILED(rc), "download cancellation fails");
        expect(!fs::exists(path), "cancelled download leaves no file");
    }

    // Catalogue fetching.
    const fs::path nro = root / "src" / "game.nro";
    test::WriteFakeNro(nro, "1.0.0");
    const std::string nro_sha = test::FileSha(nro);
    const s64 nro_size = test::FileSize(nro);
    const std::string catalogue =
        "{\"schema\":1,\"launcher\":{\"version\":\"1.0.0\",\"url\":\"\",\"sha256\":\"\",\"size\":0},"
        "\"games\":{\"remote\":{\"name\":\"Remote Game\",\"author\":\"Remote Author\","
        "\"components\":{\"nro\":{\"version\":\"1.0.0\",\"sha256\":\"" + nro_sha +
        "\",\"size\":" + std::to_string(nro_size) + ",\"url\":\"" + server.Url("/game.nro") + "\"}}},"
        "\"bad\":{\"name\":\"Bad Game\",\"author\":\"Bad\",\"components\":{\"nro\":{\"version\":\"1.0.0\","
        "\"sha256\":\"" + std::string(64, '0') + "\",\"size\":" + std::to_string(nro_size) +
        ",\"url\":\"" + server.Url("/game.nro") + "\"}}}}}";
    server.Route("/game.nro", TestResponse{200, test::ReadFile(nro)});
    server.Route("/catalogue", TestResponse{200, catalogue});
    server.Route("/bad-json", TestResponse{200, "{not json"});
    server.Route("/empty-games", TestResponse{200,
        "{\"schema\":1,\"launcher\":{\"version\":\"1.0.0\",\"url\":\"\"},\"games\":{}}"});

    std::ofstream(root / "assets.url") << server.Url("/catalogue") << "\n";

    {
        Assets fetched;
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "fetch remote catalogue");
        expect(fetched.games.contains("remote"), "remote catalogue game");
        expect(fetched.games["remote"].author == "Remote Author", "remote catalogue author");
        expect(fs::is_regular_file(root / "assets.cache.json"), "remote catalogue cached");
    }

    // A failed fetch leaves the existing cache untouched.
    {
        const std::string sentinel = "sentinel-cache";
        std::ofstream(root / "assets.cache.json") << sentinel;
        std::ofstream(root / "assets.url") << server.Url("/missing") << "\n";
        Assets fetched;
        expect(R_FAILED(FetchRemoteAssets(fetched, {})), "missing catalogue fails");
        expect(test::ReadFile(root / "assets.cache.json") == sentinel, "failed fetch keeps cache");
    }

    std::ofstream(root / "assets.url") << server.Url("/bad-json") << "\n";
    Assets invalid;
expect(R_FAILED(FetchRemoteAssets(invalid, {})), "invalid catalogue json fails");

    std::ofstream(root / "assets.url") << server.Url("/empty-games") << "\n";
    Assets empty_catalogue;
expect(R_FAILED(FetchRemoteAssets(empty_catalogue, {})), "empty catalogue fails");

    // assets.local.json must not override the network fetch.
    {
        std::ofstream(root / "assets.local.json") << catalogue;
        std::ofstream(root / "assets.url") << server.Url("/catalogue") << "\n";
        fs::remove(root / "assets.cache.json", ec);
        Assets fetched;
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "local file does not block fetch");
        expect(fetched.games.contains("remote"), "network catalogue used with local file");
        fs::remove(root / "assets.local.json", ec);
    }

    // Cancellable catalogue fetch.
    {
        server.Route("/slow-catalogue", TestResponse{200, std::string(256 * 1024, 'c'),
                                                     "application/json", "", false, false, 1, 4096});
        std::ofstream(root / "assets.url") << server.Url("/slow-catalogue") << "\n";
        int calls = 0;
        Assets fetched;
        expect(R_FAILED(FetchRemoteAssets(fetched, [&calls] { return ++calls > 1; })),
               "catalogue fetch cancellation fails");
    }

    // Banner fetch and checksum rejection.
    {
        const std::string banner = "banner bytes";
        server.Route("/banner.png", TestResponse{200, banner});
        const std::string banner_sha = [] {
            Sha256Context ctx;
            sha256ContextCreate(&ctx);
            sha256ContextUpdate(&ctx, "banner bytes", 12);
            u8 hash[32];
            sha256ContextGetHash(&ctx, hash);
            static constexpr char digits[] = "0123456789abcdef";
            std::string out;
            for (u8 byte : hash) {
                out.push_back(digits[byte >> 4]);
                out.push_back(digits[byte & 0xF]);
            }
            return out;
        }();
        const std::string with_banner =
            "{\"schema\":1,\"launcher\":{\"version\":\"1.0.0\",\"url\":\"\"},"
            "\"games\":{\"remote\":{\"name\":\"Remote Game\",\"author\":\"Remote Author\","
            "\"components\":{\"nro\":{\"version\":\"1.0.0\",\"sha256\":\"" + nro_sha +
            "\",\"size\":" + std::to_string(nro_size) + ",\"url\":\"" + server.Url("/game.nro") + "\"}},"
            "\"banner\":{\"url\":\"" + server.Url("/banner.png") + "\",\"sha256\":\"" + banner_sha +
            "\",\"size\":12}}}}";
        server.Route("/with-banner", TestResponse{200, with_banner});
        std::ofstream(root / "assets.url") << server.Url("/with-banner") << "\n";
        Assets fetched;
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "catalogue with banner");
        expect(fs::is_regular_file(root / "banners" / "remote.png"), "banner downloaded");
        expect(test::ReadFile(root / "banners" / "remote.png") == banner, "banner content");

        const std::string bad_banner =
            "{\"schema\":1,\"launcher\":{\"version\":\"1.0.0\",\"url\":\"\"},"
            "\"games\":{\"badbanner\":{\"name\":\"Bad Banner\",\"author\":\"Bad\","
            "\"components\":{\"nro\":{\"version\":\"1.0.0\",\"sha256\":\"" + nro_sha +
            "\",\"size\":" + std::to_string(nro_size) + ",\"url\":\"" + server.Url("/game.nro") + "\"}},"
            "\"banner\":{\"url\":\"" + server.Url("/banner.png") + "\",\"sha256\":\"" +
            std::string(64, '0') + "\",\"size\":12}}}}";
        server.Route("/bad-banner", TestResponse{200, bad_banner});
        std::ofstream(root / "assets.url") << server.Url("/bad-banner") << "\n";
        Assets bad_fetched;
        expect(R_SUCCEEDED(FetchRemoteAssets(bad_fetched, {})), "bad banner does not fail catalogue");
        expect(!fs::is_regular_file(root / "banners" / "badbanner.png"), "bad banner is not cached");
    }

    AssetsGame base_game;
    base_game.id = "base";
    base_game.name = "Base Game";
    base_game.nro.recomp_version = "1.0.0";
    base_game.nro.game_version = "1.0.0";
    base_game.nro.region = "U";
    base_game.nro.path = nro.string();
    base_game.nro.sha256 = nro_sha;
    base_game.nro.size = nro_size;

    // URL install.
    {
        AssetsGame game;
        game.id = "urlgame";
        game.name = "URL Game";
        game.nro.recomp_version = "1.0.0";
        game.nro.game_version = "1.0.0";
        game.nro.region = "U";
        game.nro.url = server.Url("/game.nro");
        game.nro.sha256 = nro_sha;
        game.nro.size = nro_size;
        GameEntry entry;
        entry.id = game.id;
        entry.name = game.name;
        entry.dir = (root / "games" / "urlgame").string();
        entry.nro_path = (fs::path(entry.dir) / "game.nro").string();
        std::string error;
        expect(R_SUCCEEDED(install(entry, game, error)), "url install: " + error);
        expect(fs::is_regular_file(entry.nro_path), "url install writes game.nro");

        game.nro.url = server.Url("/missing");
        GameEntry missing_entry = entry;
        missing_entry.id = "missinggame";
        missing_entry.dir = (root / "games" / "missinggame").string();
        missing_entry.nro_path = (fs::path(missing_entry.dir) / "game.nro").string();
        error.clear();
        expect(R_FAILED(install(missing_entry, game, error)), "url install 404 fails");
        expect(!fs::exists(missing_entry.nro_path), "url 404 leaves no game.nro");

        game.nro.url = server.Url("/game.nro");
        game.nro.sha256 = std::string(64, '0');
        GameEntry sha_entry = missing_entry;
        sha_entry.id = "shagame";
        sha_entry.dir = (root / "games" / "shagame").string();
        sha_entry.nro_path = (fs::path(sha_entry.dir) / "game.nro").string();
        error.clear();
        expect(R_FAILED(install(sha_entry, game, error)), "url install bad sha fails");
        expect(!fs::exists(sha_entry.nro_path), "bad sha leaves no game.nro");
    }

    // Partial URL install.
    {
        server.Route("/partial-game", TestResponse{200, test::ReadFile(nro),
                                                   "application/octet-stream", "", true});
        AssetsGame game = base_game;
        game.id = "partialgame";
        game.nro.url = server.Url("/partial-game");
        game.nro.path.clear();
        GameEntry entry;
        entry.id = game.id;
        entry.name = game.name;
        entry.dir = (root / "games" / "partialgame").string();
        entry.nro_path = (fs::path(entry.dir) / "game.nro").string();
        std::string error;
        expect(R_FAILED(install(entry, game, error)), "partial url install fails");
        expect(!fs::exists(entry.nro_path), "partial url install leaves no game.nro");
    }

    // Invalid NRO over HTTP.
    {
        const fs::path bad = root / "src" / "bad-over-http.bin";
        std::ofstream(bad, std::ios::binary) << "not an nro";
        server.Route("/bad-over-http", TestResponse{200, test::ReadFile(bad)});
        AssetsGame game = base_game;
        game.id = "badhttp";
        game.nro.url = server.Url("/bad-over-http");
        game.nro.path.clear();
        game.nro.sha256 = test::FileSha(bad);
        game.nro.size = test::FileSize(bad);
        GameEntry entry;
        entry.id = game.id;
        entry.name = game.name;
        entry.dir = (root / "games" / "badhttp").string();
        entry.nro_path = (fs::path(entry.dir) / "game.nro").string();
        std::string error;
        expect(R_FAILED(install(entry, game, error)), "invalid nro over http fails");
        expect(!fs::exists(entry.nro_path), "invalid http nro leaves no game.nro");
    }

    // RomFS over HTTP.
    {
        const std::string raw = "romfs over the network";
        const fs::path compressed = root / "src" / "network.romfs.zst";
        make_zstd(compressed, raw);
        server.Route("/network.romfs.zst", TestResponse{200, test::ReadFile(compressed)});
        AssetsGame game = base_game;
        game.id = "netromfs";
        game.name = "Network RomFS";
        game.nro.url = server.Url("/game.nro");
        game.nro.path.clear();
        game.romfs.game_version = "1.0.0";
        game.romfs.region = "U";
        game.romfs.url = server.Url("/network.romfs.zst");
        game.romfs.sha256 = test::FileSha(compressed);
        game.romfs.size = test::FileSize(compressed);
        game.romfs.image_size = static_cast<s64>(raw.size());
        game.romfs.image_sha256 = test::ShaBytes(raw);
        GameEntry entry;
        entry.id = game.id;
        entry.name = game.name;
        entry.dir = (root / "games" / "netromfs").string();
        entry.nro_path = (fs::path(entry.dir) / "game.nro").string();
        entry.romfs_path = (fs::path(entry.dir) / "assets.romfs").string();
        std::string error;
        expect(R_SUCCEEDED(install(entry, game, error)), "network romfs install: " + error);
        expect(test::ReadFile(entry.romfs_path) == raw, "network romfs content");
        expect(test::FileSha(entry.romfs_path) == game.romfs.image_sha256, "network romfs hash");
    }

    // Oversized catalogue is rejected.
    {
        server.Route("/huge-catalogue", TestResponse{200, std::string((4 * 1024 * 1024) + 16, 'x'),
                                                     "application/json"});
        std::ofstream(root / "assets.url") << server.Url("/huge-catalogue") << "\n";
        Assets fetched;
        expect(R_FAILED(FetchRemoteAssets(fetched, {})), "oversized catalogue fails");
    }

    // Launcher update over HTTP.
    {
        const fs::path launcher = root / "launcher.nro";
        test::WriteFakeNro(launcher, "1.0.0");
        const fs::path new_launcher = root / "src" / "launcher-new.nro";
        test::WriteFakeNro(new_launcher, "1.1.0");
        server.Route("/launcher.nro", TestResponse{200, test::ReadFile(new_launcher)});
        Assets assets;
        assets.launcher_version = "1.1.0";
        assets.launcher_url = server.Url("/launcher.nro");
        assets.launcher_sha256 = test::FileSha(new_launcher);
        assets.launcher_size = test::FileSize(new_launcher);
        ui::ProgressBox pbox;
        std::string error;
        bool restart = false;
        expect(R_SUCCEEDED(InstallLauncher(assets, pbox, error, restart)), "launcher update: " + error);
        expect(restart, "launcher update requests restart");
        std::string version;
        expect(R_SUCCEEDED(ReadNroVersion(launcher.string(), version)) && version == "1.1.0",
               "launcher update version");

        assets.launcher_url = server.Url("/missing");
        error.clear();
        restart = false;
        expect(R_FAILED(InstallLauncher(assets, pbox, error, restart)), "launcher update 404 fails");
        version.clear();
        expect(R_SUCCEEDED(ReadNroVersion(launcher.string(), version)) && version == "1.1.0",
               "failed launcher update keeps the old file");
    }

    server.Stop();

    if (g_failures != 0) {
        std::cerr << g_failures << " network test(s) failed\n";
        return 1;
    }
    std::cout << "launcher PC network tests passed\n";
    return 0;
}
