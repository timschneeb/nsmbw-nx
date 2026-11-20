#include "app.hpp"
#include "assets.hpp"
#include "basedata.hpp"
#include "games.hpp"
#include "install.hpp"
#include "menu_main.hpp"
#include "paths.hpp"
#include "sha.hpp"
#include "test_fixtures.hpp"
#include "test_hooks.hpp"
#include "test_http_server.hpp"
#include "ui/menus/menu_download.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"

#include "switch.h"

#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace launcher;
using launcher::ui::DownloadMenu;
using launcher::ui::MainMenu;
using launcher::ui::OptionBox;
using launcher::ui::Sidebar;

namespace {

int g_failures = 0;
int g_checks = 0;

void expect(bool condition, const std::string& what) {
    ++g_checks;
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

template<typename A, typename B>
void expect_eq(const A& actual, const B& expected, const std::string& what) {
    ++g_checks;
    if (!(actual == expected)) {
        std::cerr << "FAIL: " << what << " (got '" << actual << "', expected '" << expected << "')\n";
        ++g_failures;
    }
}

auto Escape(const std::string& value) -> std::string {
    std::string out;
    for (const char c : value) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

auto Compress(const std::string& raw) -> std::string {
    std::vector<u8> out(ZSTD_compressBound(raw.size()));
    const size_t size = ZSTD_compress(out.data(), out.size(), raw.data(), raw.size(), 3);
    out.resize(size);
    return {reinterpret_cast<const char*>(out.data()), out.size()};
}

struct GameDef {
    std::string id;
    std::string name;
    std::string author;
    std::string version;
    std::string game_version{"1.0.0"};
    std::string region{"U"};
    bool romfs{};
    std::string romfs_version;
    std::string romfs_region{"U"};
    std::string romfs_image_sha;
    s64 romfs_image_size{};
    bool banner{};
    std::string banner_sha;
    s64 banner_size{};
    bool bad_nro_sha{};
    std::string nro_url_override;
};

struct LauncherDef {
    std::string version;
    std::string url;
    bool bad_sha{};
};

struct ServerState {
    std::mutex mutex;
    std::map<std::string, GameDef> games;
    LauncherDef launcher;
    std::map<std::string, int> hits;
};

class Harness {
public:
    explicit Harness(std::string name)
        : root(fs::path("/tmp/opencode/launcher-scenarios") / std::move(name))
        , state(std::make_shared<ServerState>()) {
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "src", ec);
        fs::create_directories(root / "games", ec);
        SetRootDirectory((root / "launcher.nro").string().c_str());

        expect(server.Start(), "start scenario server");
        server.Route("/assets.json", [this](const TestRequest&) {
            Hit("/assets.json");
            return TestResponse{200, RemoteCatalogueJson(), "application/json"};
        });
        server.Route("/launcher.nro", [this](const TestRequest&) {
            Hit("/launcher.nro");
            return FileResponse(root / "src" / "launcher.nro");
        });
    }

    ~Harness() {
        server.Stop();
        test::Reset();
        launcher::ui::TestProgressCancelAfter() = -1;
        launcher::ui::TestProgressCancelCalls() = 0;
        launcher::ui::TestProgressCancelOnLabel().clear();
        launcher::ui::TestProgressCancelAfterBytes() = 0;
        launcher::ui::TestProgressEvents().clear();
    }

    auto Root() const -> const fs::path& { return root; }
    auto Src() const -> fs::path { return root / "src"; }
    auto Server() -> TestServer& { return server; }

    auto AddGame(const std::string& id, const std::string& name, const std::string& version,
                 const std::string& author = "Studio") -> GameDef {
        GameDef game;
        game.id = id;
        game.name = name;
        game.author = author;
        game.version = version;
        RegisterGameRoutes(id);
        test::WriteFakeNro(Src() / (id + ".nro"), version);
        {
            std::lock_guard lock(state->mutex);
            state->games[id] = game;
        }
        return game;
    }

    void SetGame(const GameDef& game) {
        RegisterGameRoutes(game.id);
        std::lock_guard lock(state->mutex);
        state->games[game.id] = game;
    }

    void SetVersion(const std::string& id, const std::string& version) {
        std::lock_guard lock(state->mutex);
        state->games.at(id).version = version;
    }

    void SetGameVersion(const std::string& id, const std::string& game_version) {
        std::lock_guard lock(state->mutex);
        state->games.at(id).game_version = game_version;
    }

    void SetRegion(const std::string& id, const std::string& region) {
        std::lock_guard lock(state->mutex);
        state->games.at(id).region = region;
    }

    void SetNroUrlOverride(const std::string& id, const std::string& path) {
        std::lock_guard lock(state->mutex);
        state->games.at(id).nro_url_override = path;
    }

    void RepublishNro(const std::string& id, const std::string& version, char marker = '\0',
                      size_t padding = 0) {
        test::WriteFakeNro(Src() / (id + ".nro"), version, marker, padding);
        std::lock_guard lock(state->mutex);
        auto& game = state->games.at(id);
        game.version = version;
    }

    void SetRomfs(const std::string& id, const std::string& version, const std::string& raw,
                  bool bad_image_sha = false) {
        const std::string compressed = Compress(raw);
        test::WriteFile(Src() / (id + ".romfs.zst"), compressed);
        std::lock_guard lock(state->mutex);
        auto& game = state->games.at(id);
        game.romfs = true;
        game.romfs_version = version;
        game.romfs_image_sha = bad_image_sha ? std::string(64, '0') : test::ShaBytes(raw);
        game.romfs_image_size = static_cast<s64>(raw.size());
    }

    void SetBanner(const std::string& id, const std::string& content, bool bad_sha = false) {
        test::WriteFile(Src() / (id + ".banner"), content);
        std::lock_guard lock(state->mutex);
        auto& game = state->games.at(id);
        game.banner = true;
        game.banner_sha = bad_sha ? std::string(64, '0') : test::ShaBytes(content);
        game.banner_size = static_cast<s64>(content.size());
    }

    void RemoveGame(const std::string& id) {
        std::lock_guard lock(state->mutex);
        state->games.erase(id);
    }

    void SetLauncher(const std::string& version, const std::string& url, bool bad_sha = false) {
        std::lock_guard lock(state->mutex);
        state->launcher.version = version;
        state->launcher.url = url;
        state->launcher.bad_sha = bad_sha;
    }

    void PublishLauncher(const std::string& nro_version, char marker = '\0', size_t padding = 0) {
        test::WriteFakeNro(Src() / "launcher.nro", nro_version, marker, padding);
    }

    auto RemoteCatalogueJson() -> std::string { return CatalogueJson(AllIds(), false); }

    auto LocalCatalogueJson(const std::vector<std::string>& ids) -> std::string {
        return CatalogueJson(ids, true);
    }

    void WriteLocalCatalogue(const std::vector<std::string>& ids) {
        test::WriteFile(root / "assets.json", CatalogueJson(ids, true));
        // A previously fetched cache would win at the next startup.
        std::error_code ec;
        fs::remove(root / "assets.cache.json", ec);
        fs::remove(root / "assets.local.json", ec);
    }

    void WriteLocalFile(const std::string& name, const std::string& data) {
        test::WriteFile(root / name, data);
    }

    void PointAssetsUrlToServer() {
        test::WriteFile(root / "assets.url", server.Url("/assets.json") + "\n");
    }

    void PointAssetsUrl(const std::string& url) {
        test::WriteFile(root / "assets.url", url + "\n");
    }

    void ClearAssetsUrl() {
        std::error_code ec;
        fs::remove(root / "assets.url", ec);
    }

    auto Hits(const std::string& path) -> int {
        std::lock_guard lock(state->mutex);
        return state->hits[path];
    }

    void ResetHits() {
        std::lock_guard lock(state->mutex);
        state->hits.clear();
    }

    auto GameDownloadUrl(const std::string& id) const -> std::string {
        return server.Url("/games/" + id + ".nro");
    }

private:
    auto AllIds() -> std::vector<std::string> {
        std::lock_guard lock(state->mutex);
        std::vector<std::string> ids;
        for (const auto& [id, game] : state->games) {
            (void)game;
            ids.push_back(id);
        }
        return ids;
    }

    void Hit(const std::string& path) {
        std::lock_guard lock(state->mutex);
        state->hits[path]++;
    }

    auto FileResponse(const fs::path& path) -> TestResponse {
        TestResponse response;
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            response.status = 404;
            response.body = "not found";
            return response;
        }
        response.body.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        return response;
    }

    void RegisterGameRoutes(const std::string& id) {
        std::lock_guard lock(route_mutex);
        if (!routes.insert(id).second) {
            return;
        }
        const std::string nro_path = "/games/" + id + ".nro";
        const std::string romfs_path = "/games/" + id + ".romfs.zst";
        const std::string banner_path = "/banners/" + id + ".png";
        server.Route(nro_path, [this, id, nro_path](const TestRequest&) {
            Hit(nro_path);
            return FileResponse(Src() / (id + ".nro"));
        });
        server.Route(romfs_path, [this, id, romfs_path](const TestRequest&) {
            Hit(romfs_path);
            return FileResponse(Src() / (id + ".romfs.zst"));
        });
        server.Route(banner_path, [this, id, banner_path](const TestRequest&) {
            Hit(banner_path);
            return FileResponse(Src() / (id + ".banner"));
        });
    }

    auto CatalogueJson(const std::vector<std::string>& ids, bool local) -> std::string {
        std::lock_guard lock(state->mutex);
        std::ostringstream out;
        out << "{\"schema\":1,\"recomp_version\":\"0.0.1\",\"launcher\":{";
        std::string launcher_sha = state->launcher.bad_sha ? std::string(64, '0') : std::string{};
        s64 launcher_size = 0;
        if (!state->launcher.bad_sha && !state->launcher.version.empty()) {
            launcher_sha = test::FileSha(Src() / "launcher.nro");
            launcher_size = test::FileSize(Src() / "launcher.nro");
        }
        out << "\"version\":\"" << Escape(state->launcher.version) << "\",\"url\":\""
            << Escape(state->launcher.url) << "\",\"sha256\":\"" << launcher_sha
            << "\",\"size\":" << launcher_size << "},\"games\":{";

        bool first = true;
        for (const auto& id : ids) {
            const auto it = state->games.find(id);
            if (it == state->games.end()) {
                continue;
            }
            const auto& game = it->second;
            if (!first) {
                out << ",";
            }
            first = false;

            const fs::path nro = Src() / (id + ".nro");
            std::string nro_sha = game.bad_nro_sha ? std::string(64, '0') : test::FileSha(nro);
            const std::string nro_target = local
                ? nro.string()
                : (game.nro_url_override.empty() ? GameDownloadUrl(id)
                                                 : server.Url(game.nro_url_override));

            out << "\"" << Escape(id) << "\":{\"name\":\"" << Escape(game.name)
                << "\",\"author\":\"" << Escape(game.author) << "\",\"components\":{\"nro\":{"
                << "\"recomp_version\":\"" << Escape(game.version) << "\",\"game_version\":\""
                << Escape(game.game_version) << "\",\"region\":\"" << Escape(game.region)
                << "\",\"sha256\":\"" << nro_sha
                << "\",\"size\":" << test::FileSize(nro) << ",\"" << (local ? "path" : "url")
                << "\":\"" << Escape(nro_target) << "\"}";

            if (game.romfs) {
                const fs::path zst = Src() / (id + ".romfs.zst");
                const std::string target =
                    local ? zst.string() : server.Url("/games/" + id + ".romfs.zst");
                out << ",\"romfs\":{\"game_version\":\"" << Escape(game.romfs_version)
                    << "\",\"region\":\"" << Escape(game.romfs_region)
                    << "\",\"sha256\":\"" << test::FileSha(zst) << "\",\"size\":" << test::FileSize(zst)
                    << ",\"image_sha256\":\"" << Escape(game.romfs_image_sha)
                    << "\",\"image_size\":" << game.romfs_image_size << ",\""
                    << (local ? "path" : "url") << "\":\"" << Escape(target) << "\"}";
            }
            out << "}";

            if (game.banner) {
                const std::string target =
                    local ? (Src() / (id + ".banner")).string() : server.Url("/banners/" + id + ".png");
                out << ",\"banner\":{\"" << (local ? "path" : "url") << "\":\"" << Escape(target)
                    << "\",\"sha256\":\"" << Escape(game.banner_sha) << "\",\"size\":"
                    << game.banner_size << "}";
            }
            out << "}";
        }
        out << "}}";
        return out.str();
    }

    fs::path root;
    std::shared_ptr<ServerState> state;
    std::mutex route_mutex;
    std::set<std::string> routes;
    TestServer server;
};

// ---------------------------------------------------------------------------------------------
// UI driver
// ---------------------------------------------------------------------------------------------

void press(App& app, Button button) {
    Controller controller;
    controller.m_kdown = static_cast<u64>(button);
    TouchInfo touch;
    app.TestUpdateTop(&controller, &touch);
}

auto menu_of(App& app) -> MainMenu* {
    return dynamic_cast<MainMenu*>(app.TestWidgetAt(0));
}

auto dialog_of(App& app) -> OptionBox* {
    return dynamic_cast<OptionBox*>(app.TestTop());
}

auto dialog_message(App& app) -> std::string {
    if (auto* box = dialog_of(app)) {
        return box->Message();
    }
    return {};
}

void close_dialog(App& app) {
    press(app, Button::B);
}

void confirm_dialog(App& app) {
    press(app, Button::RIGHT);
    press(app, Button::A);
}

auto row_index(const MainMenu& menu, const std::string& id) -> s64 {
    const auto& rows = menu.TestRows();
    for (size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].id == id) {
            return static_cast<s64>(i);
        }
    }
    return -1;
}

void select_row(App& app, MainMenu& menu, s64 index) {
    for (int guard = 0; guard < 64 && menu.TestIndex() > index; ++guard) {
        press(app, Button::UP);
    }
    for (int guard = 0; guard < 64 && menu.TestIndex() < index; ++guard) {
        press(app, Button::DOWN);
    }
    expect(menu.TestIndex() == index, "row selection");
}

void select_row(App& app, MainMenu& menu, const std::string& id) {
    const s64 index = row_index(menu, id);
    expect(index >= 0, "row exists: " + id);
    if (index >= 0) {
        select_row(app, menu, index);
    }
}

auto select_sidebar(App& app, const std::string& title) -> bool {
    auto* sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    if (sidebar == nullptr) {
        return false;
    }
    s64 target = -1;
    for (size_t i = 0; i < sidebar->ItemCount(); ++i) {
        if (sidebar->ItemTitle(i) == title) {
            target = static_cast<s64>(i);
            break;
        }
    }
    if (target < 0) {
        return false;
    }
    for (int guard = 0; guard < 64 && sidebar->TestIndex() > target; ++guard) {
        press(app, Button::UP);
    }
    for (int guard = 0; guard < 64 && sidebar->TestIndex() < target; ++guard) {
        press(app, Button::DOWN);
    }
    press(app, Button::A);
    return true;
}

auto sidebar_has(const Sidebar& sidebar, const std::string& title) -> bool {
    for (size_t i = 0; i < sidebar.ItemCount(); ++i) {
        if (sidebar.ItemTitle(i) == title) {
            return true;
        }
    }
    return false;
}

void update_via_options(App& app, const std::string& name) {
    press(app, Button::X);
    expect(select_sidebar(app, "Update"), "stale row offers Update");
    expect(dialog_message(app).find("Update " + name + "?") == 0, "update prompt from options");
    confirm_dialog(app);
    expect_eq(dialog_message(app), "Installed " + name + ".", "update message");
    close_dialog(app);
}

auto new_app() -> std::unique_ptr<App> {
    auto app = std::make_unique<App>();
    app->Push(std::make_unique<MainMenu>());
    app->TestSettlePop();
    return app;
}

void install_local(const std::string& id, bool expect_ok = true) {
    Assets assets;
    const Result loaded = LoadLocalAssets(assets);
    expect(R_SUCCEEDED(loaded), "load local catalogue for " + id);
    if (R_FAILED(loaded)) {
        return;
    }
    const auto catalog = assets.games.find(id);
    expect(catalog != assets.games.end(), "catalogue has " + id);
    if (catalog == assets.games.end()) {
        return;
    }

    GameEntry entry;
    entry.id = id;
    entry.name = catalog->second.name;
    entry.dir = (fs::path(GamesDirectory()) / id).string();
    entry.nro_path = (fs::path(entry.dir) / "game.nro").string();
    entry.romfs_path = (fs::path(entry.dir) / "assets.romfs").string();
    ui::ProgressBox pbox;
    std::string error;
    const Result rc = InstallGame(entry, catalog->second, pbox, error, false);
    expect(R_SUCCEEDED(rc) == expect_ok,
           "install " + id + (expect_ok ? " succeeds: " : " fails: ") + error);
}

auto make_manual(const std::string& id, const std::string& version, char marker = '\0') -> fs::path {
    const fs::path dir = fs::path(GamesDirectory()) / id;
    test::WriteFakeNro(dir / "game.nro", version, marker);
    return dir;
}

// ---------------------------------------------------------------------------------------------
// A. Catalogue parsing and acquisition
// ---------------------------------------------------------------------------------------------

void test_catalogue(Harness& h) {
    std::cout << "[catalogue]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.AddGame("beta", "Beta", "1.0.0", "Studio B");
    h.ClearAssetsUrl();
    expect_eq(AssetsUrl(), std::string(ASSETS_URL), "missing assets.url uses the default");
    const auto alpha_json = h.LocalCatalogueJson({"alpha"});
    const auto beta_json = h.LocalCatalogueJson({"beta"});
    const auto both_json = h.LocalCatalogueJson({"alpha", "beta"});

    // assets.local.json wins over the cache and the bundled catalogue.
    h.WriteLocalFile("assets.local.json", alpha_json);
    h.WriteLocalFile("assets.cache.json", beta_json);
    h.WriteLocalFile("assets.json", both_json);
    {
        Assets assets;
        expect(R_SUCCEEDED(LoadLocalAssets(assets)), "local catalogue loads");
        expect(assets.games.contains("alpha") && !assets.games.contains("beta"),
               "assets.local.json wins");
    }

    // Invalid local falls back to the cache.
    h.WriteLocalFile("assets.local.json", "{not json");
    {
        Assets assets;
        expect(R_SUCCEEDED(LoadLocalAssets(assets)), "invalid local falls back");
        expect(assets.games.contains("beta") && !assets.games.contains("alpha"),
               "cache used after invalid local");
    }

    // Invalid cache falls back to the bundled catalogue.
    std::error_code ec;
    fs::remove(h.Root() / "assets.local.json", ec);
    h.WriteLocalFile("assets.cache.json", "{also not json");
    {
        Assets assets;
        expect(R_SUCCEEDED(LoadLocalAssets(assets)), "invalid cache falls back");
        expect(assets.games.contains("alpha") && assets.games.contains("beta"),
               "bundled catalogue used");
    }

    // No catalogue at all.
    fs::remove(h.Root() / "assets.json", ec);
    {
        Assets assets;
        expect(R_FAILED(LoadLocalAssets(assets)), "missing catalogue fails");
        expect(!assets.valid, "missing catalogue is invalid");
    }

    // Remote fetch always wins, even with a local, cache, and bundled file present.
    h.WriteLocalFile("assets.local.json", alpha_json);
    h.WriteLocalFile("assets.cache.json", alpha_json);
    h.WriteLocalFile("assets.json", alpha_json);
    h.PointAssetsUrlToServer();
    fs::remove(h.Root() / "assets.cache.json", ec);
    {
        Assets fetched;
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "remote catalogue fetches");
        expect(fetched.games.size() == 2 && fetched.games.contains("alpha") &&
                   fetched.games.contains("beta"),
               "remote catalogue replaces local files");
        expect(fs::is_regular_file(h.Root() / "assets.cache.json"), "remote catalogue cached");
        expect(!fs::exists(h.Root() / "assets.cache.json.tmp"), "cache write leaves no tmp");
        expect_eq(fetched.recomp_version, std::string("0.0.1"), "recomp version parsed");
        expect_eq(fetched.games["alpha"].author, std::string("Studio A"), "author parsed");
        expect_eq(fetched.games["alpha"].nro.recomp_version, std::string("1.0.0"), "nro recomp version parsed");
        expect_eq(fetched.games["alpha"].nro.game_version, std::string("1.0.0"), "nro game version parsed");
        expect_eq(fetched.games["alpha"].nro.region, std::string("U"), "nro region parsed");
    }

    // A failed fetch keeps the previous cache byte for byte.
    {
        const std::string sentinel = "sentinel-cache";
        h.WriteLocalFile("assets.cache.json", sentinel);
        h.PointAssetsUrl(h.Server().Url("/missing"));
        Assets fetched;
        expect(R_FAILED(FetchRemoteAssets(fetched, {})), "missing catalogue fails");
        expect_eq(test::ReadFile(h.Root() / "assets.cache.json"), sentinel, "failed fetch keeps cache");
        expect(!fs::exists(h.Root() / "assets.cache.json.tmp"), "failed fetch leaves no tmp");
    }

    // Unsafe ids and data-only entries are skipped.
    {
        h.WriteLocalFile("assets.url", "");
        h.Server().Route("/unsafe", TestResponse{200,
            "{\"schema\":1,\"launcher\":{\"version\":\"1.0.0\",\"url\":\"\"},\"games\":{"
            "\"../evil\":{\"name\":\"Evil\",\"components\":{\"nro\":{\"version\":\"1.0.0\","
            "\"sha256\":\"" + std::string(64, '0') + "\",\"size\":1,\"url\":\"http://x/\"}}},"
            "\"dataless\":{\"name\":\"Data\",\"author\":\"X\",\"components\":{}}}}"});
        h.PointAssetsUrl(h.Server().Url("/unsafe"));
        Assets fetched;
        expect(R_FAILED(FetchRemoteAssets(fetched, {})), "unsafe-only catalogue fails");
    }
    {
        h.Server().Route("/mixed", TestResponse{200,
            "{\"schema\":1,\"launcher\":{\"version\":\"1.0.0\",\"url\":\"\"},\"games\":{"
            "\"../evil\":{\"name\":\"Evil\",\"components\":{\"nro\":{\"version\":\"1.0.0\","
            "\"sha256\":\"" + std::string(64, '0') + "\",\"size\":1,\"url\":\"http://x/\"}}},"
            "\"good\":{\"name\":\"Good\",\"author\":\"G\",\"components\":{\"nro\":"
            "{\"version\":\"1.0.0\",\"sha256\":\"" + test::FileSha(h.Src() / "alpha.nro") +
            "\",\"size\":" + std::to_string(test::FileSize(h.Src() / "alpha.nro")) + ",\"url\":\"" +
            h.GameDownloadUrl("alpha") + "\"}}}}}"});
        h.PointAssetsUrl(h.Server().Url("/mixed"));
        Assets fetched;
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "mixed catalogue fetches");
        expect(!fetched.games.contains("../evil"), "unsafe id skipped");
        expect(fetched.games.contains("good"), "valid id kept");
        expect_eq(fetched.games["good"].author, std::string("G"), "optional author");
    }

    // Banners: download once, verify SHA, never fetch twice while valid.
    {
        h.SetBanner("alpha", "banner-bytes");
        h.PointAssetsUrlToServer();
        h.ResetHits();
        Assets fetched;
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "banner catalogue fetches");
        expect(fs::is_regular_file(h.Root() / "banners" / "alpha.png"), "banner downloaded");
        expect_eq(test::ReadFile(h.Root() / "banners" / "alpha.png"), std::string("banner-bytes"),
                  "banner content");
        expect_eq(h.Hits("/banners/alpha.png"), 1, "banner requested once");
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "second banner fetch succeeds");
        expect_eq(h.Hits("/banners/alpha.png"), 1, "valid banner is not re-requested");

        // A new catalogue SHA replaces the local banner.
        h.SetBanner("alpha", "banner-two");
        h.PointAssetsUrlToServer();
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "changed banner fetch succeeds");
        expect_eq(h.Hits("/banners/alpha.png"), 2, "changed banner is re-requested");
        expect_eq(test::ReadFile(h.Root() / "banners" / "alpha.png"), std::string("banner-two"),
                  "changed banner replaced");
    }

    // A bad banner hash never leaves a file behind.
    {
        h.SetBanner("beta", "bad-banner", true);
        h.PointAssetsUrlToServer();
        Assets fetched;
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "bad banner catalogue fetches");
        expect(!fs::exists(h.Root() / "banners" / "beta.png"), "bad banner not cached");
        expect(!fs::exists(h.Root() / "banners" / "beta.png.part"), "bad banner leaves no part");
    }

    // A cancelled fetch writes no cache.
    {
        h.Server().Route("/slow-catalogue", TestResponse{200, std::string(256 * 1024, 'c'),
                                                         "application/json", "", false, false, 1,
                                                         4096});
        h.PointAssetsUrl(h.Server().Url("/slow-catalogue"));
        fs::remove(h.Root() / "assets.cache.json", ec);
        int calls = 0;
        Assets fetched;
        const Result rc = FetchRemoteAssets(fetched, [&calls] { return ++calls > 1; });
        expect(R_FAILED(rc), "cancelled catalogue fetch fails");
        expect(!fs::exists(h.Root() / "assets.cache.json"), "cancelled fetch writes no cache");
    }
}

// ---------------------------------------------------------------------------------------------
// B/C/D. Install and the download menu
// ---------------------------------------------------------------------------------------------

void test_download_and_install(Harness& h) {
    std::cout << "[download]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.AddGame("beta", "Beta", "1.0.0", "Studio B");
    h.SetRomfs("alpha", "1.0.0", std::string(4096, 'a'));
    h.WriteLocalCatalogue({"alpha", "beta"});
    h.PointAssetsUrlToServer();

    auto app = new_app();
    auto* menu = menu_of(*app);
    expect(menu != nullptr, "main menu is open");
    if (menu == nullptr) {
        return;
    }

    expect(menu->TestRows().empty(), "uninstalled games are not main-menu rows");
    expect_eq(menu->TestRowCount(), s64(1), "only the download row");
    expect_eq(menu->m_download_count, s64(2), "download count");

    // The download row hides X and opens the page.
    select_row(*app, *menu, 0);
    expect(menu->m_actions.at(Button::X).m_hint.empty(), "X hidden on download row");
    expect_eq(menu->m_actions.at(Button::A).m_hint, std::string("Open"), "download hint");
    h.ResetHits();
    press(*app, Button::A);

    auto* download = dynamic_cast<DownloadMenu*>(app->TestTop());
    expect(download != nullptr, "download page opened");
    if (download == nullptr) {
        return;
    }
    expect(download->TestLoaded(), "download page fetched");
    expect_eq(h.Hits("/assets.json"), 1, "download page fetches once");
    press(*app, Button::DOWN);
    press(*app, Button::UP);
    expect_eq(h.Hits("/assets.json"), 1, "navigation does not refetch the catalogue");
    expect_eq(download->TestRows().size(), size_t(2), "download lists both games");
    expect_eq(download->TestRows()[0].id, std::string("alpha"), "download sorted by name");
    expect_eq(download->TestRows()[0].author, std::string("Studio A"), "download author");

    // Cancel the install prompt.
    press(*app, Button::A);
    auto* prompt = dialog_of(*app);
    expect(prompt != nullptr, "install prompt shown");
    if (prompt != nullptr) {
        const auto text = prompt->Message();
        expect(text.find("Install Alpha?") != std::string::npos, "install prompt action");
        expect(text.find("Download") != std::string::npos, "install prompt size");
        expect(text.find("Needs") != std::string::npos, "install prompt free space");
        const auto options = prompt->TestOptions();
        expect(options.size() == 2 && options[0] == "Cancel" && options[1] == "Install",
               "install prompt options");
        press(*app, Button::A);
    }
    expect(!fs::exists(fs::path(GamesDirectory()) / "alpha" / "game.nro"),
           "cancelled install writes nothing");

    // Install alpha, including RomFS.
    press(*app, Button::A);
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Installed Alpha."), "install message");
    close_dialog(*app);

    const fs::path alpha_dir = fs::path(GamesDirectory()) / "alpha";
    expect(fs::is_regular_file(alpha_dir / "game.nro"), "alpha game.nro installed");
    expect(fs::is_regular_file(alpha_dir / "assets.romfs"), "alpha romfs installed");
    expect(fs::is_regular_file(alpha_dir / "installed.json"), "alpha installed.json");
    {
        const Installed installed = ReadInstalled(alpha_dir.string());
        expect(installed.present, "alpha metadata present");
        expect_eq(installed.nro.recomp_version, std::string("1.0.0"), "alpha nro version recorded");
        expect_eq(installed.romfs.game_version, std::string("1.0.0"), "alpha romfs version recorded");
        expect_eq(installed.romfs.size, s64(4096), "alpha romfs size recorded");
        expect_eq(test::ReadFile(alpha_dir / "game.nro"), test::ReadFile(h.Src() / "alpha.nro"),
                  "alpha nro content");
    }
    expect(test::ReadFile(h.Root() / "usage.json").find("\"alpha\"") != std::string::npos,
           "install records usage");
    expect_eq(download->TestRows().size(), size_t(1), "installed game leaves download list");

    // Install beta, then the page is empty.
    press(*app, Button::A);
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Installed Beta."), "beta install message");
    close_dialog(*app);
    expect(download->TestRows().empty(), "all games installed");

    // Back to the main menu refreshes the rows.
    press(*app, Button::B);
    expect(app->TestTop() == menu, "returned to the main menu");
    expect_eq(menu->TestRows().size(), size_t(2), "installed games listed");
    expect(menu->TestRows()[0].state == GameState::Launch, "installed row is launchable");
    expect_eq(menu->m_download_count, s64(0), "download count is zero");

    // Game options show the game and recomp versions.
    select_row(*app, *menu, "alpha");
    press(*app, Button::X);
    {
        auto* game_sidebar = dynamic_cast<Sidebar*>(app->TestTop());
        expect(game_sidebar != nullptr && sidebar_has(*game_sidebar, "Version info"),
               "version info entry");
        if (game_sidebar != nullptr) {
            expect(select_sidebar(*app, "Version info"), "version info selected");
            const auto info = dialog_message(*app);
            expect(info.find("Game version: 1.0.0") != std::string::npos,
                   "version info game version");
            expect(info.find("Recomp version: 1.0.0") != std::string::npos,
                   "version info recomp version");
            close_dialog(*app);
        }
    }

    // A new remote game appears after reopening the page.
    h.AddGame("gamma", "Gamma", "1.0.0", "Studio C");
    select_row(*app, *menu, menu->TestRowCount() - 1);
    press(*app, Button::A);
    download = dynamic_cast<DownloadMenu*>(app->TestTop());
    expect(download != nullptr && download->TestRows().size() == 1 &&
               download->TestRows()[0].id == "gamma",
           "new remote game appears in the download page");

    // B quits the launcher at the root.
    press(*app, Button::B);
    press(*app, Button::B);
    expect(app->IsQuitting(), "B quits at the root");
}

// ---------------------------------------------------------------------------------------------
// E/G. Updates, partial updates, verify and reinstall
// ---------------------------------------------------------------------------------------------

void test_updates(Harness& h) {
    std::cout << "[updates]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.SetRomfs("alpha", "1.0.0", std::string(5000, 'o'));
    h.WriteLocalCatalogue({"alpha"});
    h.PointAssetsUrlToServer();
    install_local("alpha");

    auto app = new_app();
    auto* menu = menu_of(*app);
    expect(menu != nullptr, "update menu opens");
    if (menu == nullptr) {
        return;
    }

    const auto nro_path = fs::path(GamesDirectory()) / "alpha" / "game.nro";
    const auto romfs_path = fs::path(GamesDirectory()) / "alpha" / "assets.romfs";
    const auto romfs_bytes = test::ReadFile(romfs_path);
    const auto romfs_time = fs::last_write_time(romfs_path);

    // NRO-only update: the RomFS must not be requested.
    h.RepublishNro("alpha", "1.1.0", 'n');
    select_row(*app, *menu, "alpha");
    press(*app, Button::X);
    auto* sidebar = dynamic_cast<Sidebar*>(app->TestTop());
    expect(sidebar != nullptr, "game options open");
    if (sidebar == nullptr) {
        return;
    }
    expect(sidebar_has(*sidebar, "Check for updates"), "current game checks updates");
    expect(!sidebar_has(*sidebar, "Update"), "current game has no Update entry");
    expect(sidebar_has(*sidebar, "Reinstall"), "reinstall entry");
    expect(sidebar_has(*sidebar, "Verify files"), "verify entry");
    expect(sidebar_has(*sidebar, "Delete game"), "delete entry");
    expect(sidebar_has(*sidebar, "Delete game and save data"), "delete and save entry");
    h.ResetHits();
    expect(select_sidebar(*app, "Check for updates"), "check updates selected");
    {
        const auto text = dialog_message(*app);
        expect(text.find("Update Alpha?") == 0, "update prompt after check");
        expect(text.find("Download") != std::string::npos, "update prompt shows download size");
        expect(text.find("Needs") != std::string::npos, "update prompt shows free space");
        const auto* box = dialog_of(*app);
        expect(box != nullptr && box->TestOptions().size() == 2, "update prompt options");
        expect(box != nullptr && box->TestIndex() == 1, "update prompt defaults to Update");
        close_dialog(*app); // cancel
    }
    {
        const Installed installed = ReadInstalled((fs::path(GamesDirectory()) / "alpha").string());
        expect_eq(installed.nro.recomp_version, std::string("1.0.0"), "cancelled update keeps version");
    }

    // The failed check left the row stale, so Game options now offers Update directly.
    press(*app, Button::X);
    sidebar = dynamic_cast<Sidebar*>(app->TestTop());
    expect(sidebar != nullptr && sidebar_has(*sidebar, "Update"), "stale game offers Update");
    expect(sidebar != nullptr && !sidebar_has(*sidebar, "Check for updates"),
           "stale game hides Check for updates");
    expect(select_sidebar(*app, "Update"), "Update entry selected");
    expect(dialog_message(*app).find("Update Alpha?") == 0, "Update entry prompt");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Installed Alpha."), "update message");
    close_dialog(*app);
    {
        const Installed installed = ReadInstalled((fs::path(GamesDirectory()) / "alpha").string());
        expect_eq(installed.nro.recomp_version, std::string("1.1.0"), "nro updated");
        expect_eq(test::ReadFile(nro_path), test::ReadFile(h.Src() / "alpha.nro"), "updated nro content");
        expect_eq(test::ReadFile(romfs_path), romfs_bytes, "romfs untouched by nro update");
        expect(fs::last_write_time(romfs_path) == romfs_time, "romfs mtime untouched");
    }
    expect_eq(h.Hits("/games/alpha.romfs.zst"), 0, "nro-only update does not request romfs");
    expect(menu->TestRows()[0].state == GameState::Launch, "row current after update");

    // RomFS-only update keeps the NRO.
    h.SetRomfs("alpha", "2.0.0", std::string(7000, 'r'));
    const auto nro_bytes = test::ReadFile(nro_path);
    const auto nro_time = fs::last_write_time(nro_path);
    h.ResetHits();
    press(*app, Button::X);
    select_sidebar(*app, "Check for updates");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Installed Alpha."), "romfs update message");
    close_dialog(*app);
    {
        const Installed installed = ReadInstalled((fs::path(GamesDirectory()) / "alpha").string());
        expect_eq(installed.romfs.game_version, std::string("2.0.0"), "romfs updated");
        expect_eq(test::ReadFile(nro_path), nro_bytes, "nro untouched by romfs update");
        expect(fs::last_write_time(nro_path) == nro_time, "nro mtime untouched");
        expect_eq(test::FileSize(romfs_path), s64(7000), "romfs image size");
    }
    expect_eq(h.Hits("/games/alpha.nro"), 0, "romfs-only update does not request nro");

    // Up to date check.
    press(*app, Button::X);
    select_sidebar(*app, "Check for updates");
    expect_eq(dialog_message(*app), std::string("Alpha is up to date."), "up to date dialog");
    close_dialog(*app);

    // A launches a stale row; updates only from Game options.
    h.RepublishNro("alpha", "1.2.0", 'u');
    select_row(*app, *menu, menu->TestRowCount() - 1);
    press(*app, Button::A); // download row, triggers a catalogue refresh
    expect(dynamic_cast<DownloadMenu*>(app->TestTop()) != nullptr, "download page refresh");
    press(*app, Button::B);
    expect(menu->TestRows()[0].state == GameState::Update, "fetch marks the row stale");
    select_row(*app, *menu, 0);
    press(*app, Button::A);
    expect(app->IsQuitting(), "stale row launches");
    expect_eq(app->LaunchedPath(), (fs::path(GamesDirectory()) / "alpha" / "game.nro").string(),
              "stale row launch path");
    update_via_options(*app, "Alpha");
    expect(menu->TestRows()[0].state == GameState::Launch, "row current after options update");
    expect_eq(fs::path(GamesDirectory()).empty(), false, "games folder exists");

    // Same version, same size, different bytes: not detected as an update, but Verify finds it.
    test::WriteFakeNro(h.Src() / "alpha.nro", "1.2.0", 'x');
    fs::copy_file(h.Src() / "alpha.nro", fs::path(GamesDirectory()) / "alpha" / "game.nro",
                  fs::copy_options::overwrite_existing);
    h.SetVersion("alpha", "1.2.0");
    select_row(*app, *menu, menu->TestRowCount() - 1);
    press(*app, Button::A);
    press(*app, Button::B);
    expect(menu->TestRows()[0].state == GameState::Launch, "same version/size is not an update");
    select_row(*app, *menu, 0);
    press(*app, Button::X);
    select_sidebar(*app, "Verify files");
    expect_eq(dialog_message(*app), std::string("Checksum mismatch for game.nro."),
              "verify catches a silent republish");
    close_dialog(*app);

    // Reinstall force refreshes the checksums.
    press(*app, Button::X);
    select_sidebar(*app, "Reinstall");
    expect(dialog_message(*app).find("Reinstall Alpha?") == 0, "reinstall prompt");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Installed Alpha."), "reinstall message");
    close_dialog(*app);
    press(*app, Button::X);
    select_sidebar(*app, "Verify files");
    expect_eq(dialog_message(*app), std::string("Files verified for Alpha."), "verify after reinstall");
    close_dialog(*app);

    // Same version, different size is not an update: detection is version only.
    {
        std::ofstream file(h.Src() / "alpha.nro", std::ios::binary | std::ios::app);
        file.put('\0');
    }
    select_row(*app, *menu, menu->TestRowCount() - 1);
    press(*app, Button::A);
    press(*app, Button::B);
    expect(menu->TestRows()[0].state == GameState::Launch, "size change is not an update");
    select_row(*app, *menu, 0);
    press(*app, Button::A);
    expect(app->IsQuitting(), "size change row launches");
    // Y does nothing when the selected row is not updatable.
    press(*app, Button::Y);
    expect(dialog_of(*app) == nullptr, "Y ignores a current row");

    // Changing only the game version triggers an update and records it separately.
    h.SetGameVersion("alpha", "1.1.0");
    select_row(*app, *menu, menu->TestRowCount() - 1);
    press(*app, Button::A);
    press(*app, Button::B);
    expect(menu->TestRows()[0].state == GameState::Update, "game version change marks the row stale");
    select_row(*app, *menu, 0);
    press(*app, Button::A);
    expect(app->IsQuitting(), "game version row launches");
    // Y starts the same update prompt as Game options.
    press(*app, Button::Y);
    expect(dialog_message(*app).find("Update Alpha?") == 0, "Y starts the update");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Installed Alpha."), "Y update message");
    close_dialog(*app);
    {
        const Installed installed = ReadInstalled((fs::path(GamesDirectory()) / "alpha").string());
        expect_eq(installed.nro.game_version, std::string("1.1.0"), "game version updated");
        expect_eq(installed.nro.recomp_version, std::string("1.2.0"), "recomp version kept");
    }

    // Changing only the region triggers an update and records the region.
    h.SetRegion("alpha", "E");
    select_row(*app, *menu, menu->TestRowCount() - 1);
    press(*app, Button::A);
    press(*app, Button::B);
    expect(menu->TestRows()[0].state == GameState::Update, "region change marks the row stale");
    select_row(*app, *menu, 0);
    press(*app, Button::A);
    expect(app->IsQuitting(), "region row launches");
    update_via_options(*app, "Alpha");
    {
        const Installed installed = ReadInstalled((fs::path(GamesDirectory()) / "alpha").string());
        expect_eq(installed.nro.region, std::string("E"), "region updated");
    }

    // A downgrade is offered like any other version change.
    h.RepublishNro("alpha", "1.0.0", 'D');
    select_row(*app, *menu, menu->TestRowCount() - 1);
    press(*app, Button::A);
    press(*app, Button::B);
    expect(menu->TestRows()[0].state == GameState::Update, "downgrade marks the row stale");
    select_row(*app, *menu, 0);
    press(*app, Button::A);
    expect(app->IsQuitting(), "downgrade row launches");
    update_via_options(*app, "Alpha");
    {
        const Installed installed = ReadInstalled((fs::path(GamesDirectory()) / "alpha").string());
        expect_eq(installed.nro.recomp_version, std::string("1.0.0"), "downgrade applied");
    }

    // A failed update check reports the network error.
    h.PointAssetsUrl(h.Server().Url("/missing"));
    press(*app, Button::X);
    select_sidebar(*app, "Check for updates");
    expect_eq(dialog_message(*app),
              std::string("Could not check for updates. Check the network connection."),
              "game update network failure");
    close_dialog(*app);
    h.PointAssetsUrlToServer();
}

// ---------------------------------------------------------------------------------------------
// Transactional NRO plus RomFS updates
// ---------------------------------------------------------------------------------------------

void test_transactional_update(Harness& h) {
    std::cout << "[transaction]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.SetRomfs("alpha", "1.0.0", std::string(8192, 'a'));
    h.WriteLocalCatalogue({"alpha"});
    h.PointAssetsUrlToServer();
    install_local("alpha");

    const fs::path dir = fs::path(GamesDirectory()) / "alpha";
    const auto nro_before = test::ReadFile(dir / "game.nro");
    const auto romfs_before = test::ReadFile(dir / "assets.romfs");
    const auto json_before = test::ReadFile(dir / "installed.json");

    h.RepublishNro("alpha", "1.1.0", 'n');
    h.SetRomfs("alpha", "1.1.0", std::string(8192, 'b'));
    h.WriteLocalCatalogue({"alpha"});

    Assets assets;
    expect(R_SUCCEEDED(LoadLocalAssets(assets)), "transaction catalogue loads");
    GameEntry entry;
    bool found = false;
    for (const auto& candidate : ScanGames(assets)) {
        if (candidate.id == "alpha") {
            entry = candidate;
            found = true;
            break;
        }
    }
    expect(found, "transaction entry found");
    if (!found) {
        return;
    }

    {
        ui::ProgressBox pbox;
        launcher::ui::TestProgressCancelOnLabel() = "assets.romfs";
        launcher::ui::TestProgressCancelAfterBytes() = 0;
        std::string error;
        const Result rc = InstallGame(entry, assets.games.at("alpha"), pbox, error, false);
        expect(R_FAILED(rc), "cancelled transaction fails");
        expect_eq(error, std::string("Install cancelled."), "transaction cancel message");
    }
    launcher::ui::TestProgressCancelOnLabel().clear();
    launcher::ui::TestProgressCancelAfterBytes() = 0;
    launcher::ui::TestProgressCancelAfter() = -1;
    launcher::ui::TestProgressCancelCalls() = 0;

    expect_eq(test::ReadFile(dir / "game.nro"), nro_before, "cancelled transaction keeps nro");
    expect_eq(test::ReadFile(dir / "assets.romfs"), romfs_before,
              "cancelled transaction keeps romfs");
    expect_eq(test::ReadFile(dir / "installed.json"), json_before,
              "cancelled transaction keeps metadata");
    expect(!fs::exists(dir / "game.nro.part"), "cancelled transaction leaves no nro part");
    expect(!fs::exists(dir / "assets.romfs.part"), "cancelled transaction leaves no romfs part");

    {
        ui::ProgressBox pbox;
        launcher::ui::TestProgressEvents().clear();
        std::string error;
        const Result rc = InstallGame(entry, assets.games.at("alpha"), pbox, error, false);
        expect(R_SUCCEEDED(rc), "transaction retry succeeds: " + error);
    }

    const Installed installed = ReadInstalled(dir.string());
    expect_eq(installed.nro.recomp_version, std::string("1.1.0"), "transaction nro updated");
    expect_eq(installed.romfs.game_version, std::string("1.1.0"), "transaction romfs updated");
    expect_eq(test::ReadFile(dir / "game.nro"), test::ReadFile(h.Src() / "alpha.nro"),
              "transaction nro content");
    expect_eq(test::ReadFile(dir / "assets.romfs"), std::string(8192, 'b'),
              "transaction romfs content");

    const auto& events = launcher::ui::TestProgressEvents();
    expect(!events.empty(), "combined progress events");
    bool monotonic = true;
    for (size_t i = 1; i < events.size(); ++i) {
        if (events[i].first < events[i - 1].first) {
            monotonic = false;
        }
    }
    expect(monotonic, "combined progress is monotonic");
    const s64 expected_total = installed.nro.size + installed.romfs.size;
    expect(!events.empty() && events.back().first == expected_total,
           "combined progress reaches total offset");
    expect(!events.empty() && events.back().second == expected_total,
           "combined progress reaches total size");
}

// ---------------------------------------------------------------------------------------------
// G. Update all
// ---------------------------------------------------------------------------------------------

void test_update_all(Harness& h) {
    std::cout << "[update all]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.AddGame("beta", "Beta", "1.0.0", "Studio B");
    h.WriteLocalCatalogue({"alpha", "beta"});
    h.PointAssetsUrlToServer();
    install_local("alpha");
    install_local("beta");

    auto app = new_app();
    auto* menu = menu_of(*app);
    expect(menu != nullptr, "update-all menu opens");
    if (menu == nullptr) {
        return;
    }

    h.RepublishNro("alpha", "1.1.0", 'a');
    h.RepublishNro("beta", "1.1.0", 'b');

    press(*app, Button::SELECT);
    auto* settings = dynamic_cast<Sidebar*>(app->TestTop());
    expect(settings != nullptr, "settings opens");
    if (settings == nullptr) {
        return;
    }
    expect(settings->Title() == "Settings", "settings title");
    expect(sidebar_has(*settings, "Update all"), "update all entry");
    expect(sidebar_has(*settings, "Check launcher update"), "check launcher entry");
    expect(sidebar_has(*settings, "About"), "about entry");
    expect(!sidebar_has(*settings, "Base game data"), "base data entry removed");
    expect(!sidebar_has(*settings, "Clear cached catalogue"), "cached catalogue entry removed");

    expect(select_sidebar(*app, "Update all"), "update all selected");
    {
        const auto text = dialog_message(*app);
        expect(text.find("Update 2 games?") == 0, "update all confirmation");
        expect(text.find("Download") != std::string::npos, "update all download size");
        close_dialog(*app);
    }
    {
        const Installed installed = ReadInstalled((fs::path(GamesDirectory()) / "alpha").string());
        expect_eq(installed.nro.recomp_version, std::string("1.0.0"), "update all cancel changes nothing");
    }

    press(*app, Button::SELECT);
    select_sidebar(*app, "Update all");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("All games updated."), "update all message");
    close_dialog(*app);
    {
        const Installed alpha = ReadInstalled((fs::path(GamesDirectory()) / "alpha").string());
        const Installed beta = ReadInstalled((fs::path(GamesDirectory()) / "beta").string());
        expect_eq(alpha.nro.recomp_version, std::string("1.1.0"), "alpha updated by update all");
        expect_eq(beta.nro.recomp_version, std::string("1.1.0"), "beta updated by update all");
    }

    // Nothing to do.
    press(*app, Button::SELECT);
    select_sidebar(*app, "Update all");
    expect_eq(dialog_message(*app), std::string("Everything is up to date."),
              "update all with no updates");
    close_dialog(*app);

    // One game fails; the other stays updated.
    h.RepublishNro("alpha", "1.2.0", 'c');
    h.RepublishNro("beta", "1.2.0", 'd');
    h.SetNroUrlOverride("beta", "/missing");
    press(*app, Button::SELECT);
    select_sidebar(*app, "Update all");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Download failed for game.nro."),
              "update all reports the failing game");
    close_dialog(*app);
    {
        const Installed alpha = ReadInstalled((fs::path(GamesDirectory()) / "alpha").string());
        const Installed beta = ReadInstalled((fs::path(GamesDirectory()) / "beta").string());
        expect_eq(alpha.nro.recomp_version, std::string("1.2.0"), "update all kept the successful game");
        expect_eq(beta.nro.recomp_version, std::string("1.1.0"), "update all left the failed game");
        expect(menu->TestRows()[0].state == GameState::Launch ||
                   menu->TestRows()[1].state == GameState::Update,
               "failed update leaves a retryable row");
    }

    // Retry only downloads the remaining game.
    h.SetNroUrlOverride("beta", "");
    const auto alpha_bytes = test::ReadFile(fs::path(GamesDirectory()) / "alpha" / "game.nro");
    press(*app, Button::SELECT);
    select_sidebar(*app, "Update all");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("All games updated."), "update all retry message");
    close_dialog(*app);
    {
        const Installed beta = ReadInstalled((fs::path(GamesDirectory()) / "beta").string());
        expect_eq(beta.nro.recomp_version, std::string("1.2.0"), "retry updates the remaining game");
        expect_eq(test::ReadFile(fs::path(GamesDirectory()) / "beta" / "game.nro"),
                  test::ReadFile(h.Src() / "beta.nro"), "retry rewrites the failed game");
        expect_eq(test::ReadFile(fs::path(GamesDirectory()) / "alpha" / "game.nro"), alpha_bytes,
                  "retry does not touch the updated game");
    }

    // A network failure while refreshing the catalogue leaves everything as is.
    h.PointAssetsUrl(h.Server().Url("/missing"));
    press(*app, Button::SELECT);
    select_sidebar(*app, "Update all");
    expect_eq(dialog_message(*app),
              std::string("Could not check for updates. Check the network connection."),
              "update all network failure message");
    close_dialog(*app);
    h.PointAssetsUrlToServer();
}

// ---------------------------------------------------------------------------------------------
// K. Persistence and cleanup
// ---------------------------------------------------------------------------------------------

void test_cleanup_and_io(Harness& h) {
    std::cout << "[cleanup]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.WriteLocalCatalogue({"alpha"});
    install_local("alpha");
    const auto nro = test::ReadFile(fs::path(GamesDirectory()) / "alpha" / "game.nro");

    h.WriteLocalFile("launcher.nro.part", "x");
    h.WriteLocalFile("launcher.nro.new", "x");
    h.WriteLocalFile("launcher.nro.new.part", "x");
    h.WriteLocalFile("launcher.nro.old", "x");
    h.WriteLocalFile("assets.cache.json.tmp", "x");
    h.WriteLocalFile("usage.json.tmp", "x");
    h.WriteLocalFile("banners/stale.png.part", "x");
    h.WriteLocalFile("games/alpha/game.nro.part", "x");
    h.WriteLocalFile("games/alpha/assets.romfs.part", "x");
    h.WriteLocalFile("games/alpha/installed.json.tmp", "x");
    h.WriteLocalFile("games/trash.removed/junk", "x");

    CleanupPendingInstalls();

    expect(!fs::exists(h.Root() / "launcher.nro.part"), "cleanup launcher.part");
    expect(!fs::exists(h.Root() / "launcher.nro.new"), "cleanup launcher.new");
    expect(!fs::exists(h.Root() / "launcher.nro.new.part"), "cleanup launcher.new.part");
    expect(!fs::exists(h.Root() / "launcher.nro.old"), "cleanup launcher.old");
    expect(!fs::exists(h.Root() / "assets.cache.json.tmp"), "cleanup cache tmp");
    expect(!fs::exists(h.Root() / "usage.json.tmp"), "cleanup usage tmp");
    expect(!fs::exists(h.Root() / "banners" / "stale.png.part"), "cleanup banner part");
    expect(!fs::exists(fs::path(GamesDirectory()) / "alpha" / "game.nro.part"), "cleanup game part");
    expect(!fs::exists(fs::path(GamesDirectory()) / "alpha" / "assets.romfs.part"),
           "cleanup romfs part");
    expect(!fs::exists(fs::path(GamesDirectory()) / "alpha" / "installed.json.tmp"),
           "cleanup metadata tmp");
    expect(!fs::exists(fs::path(GamesDirectory()) / "trash.removed"), "cleanup removed trash");
    expect_eq(test::ReadFile(fs::path(GamesDirectory()) / "alpha" / "game.nro"), nro,
              "cleanup keeps installed files");

    // usage.json invalid JSON is ignored, then rewritten with escaped ids.
    h.WriteLocalFile("usage.json", "{not json");
    TouchUsage("we\"ird\\id");
    {
        const auto usage = test::ReadFile(h.Root() / "usage.json");
        expect(usage.find("we\\\"ird\\\\id") != std::string::npos, "usage id escaped");
        expect(usage.find("\"we\\\"ird\\\\id\": 1") != std::string::npos, "usage entry written");
    }

    // Metadata write failure: installed.json.tmp is a directory.
    {
        const fs::path dir = fs::path(GamesDirectory()) / "alpha";
        fs::create_directory(dir / "installed.json.tmp");
        Assets assets;
        LoadLocalAssets(assets);
        GameEntry entry;
        entry.id = "alpha";
        entry.name = "Alpha";
        entry.dir = dir.string();
        entry.nro_path = (dir / "game.nro").string();
        entry.romfs_path = (dir / "assets.romfs").string();
        ui::ProgressBox pbox;
        std::string error;
        const Result rc = InstallGame(entry, assets.games.at("alpha"), pbox, error, true);
        expect(R_FAILED(rc), "metadata write failure fails the install");
        expect_eq(error, std::string("Could not write installed.json."), "metadata write error");
        fs::remove(dir / "installed.json.tmp");
        error.clear();
        expect(R_SUCCEEDED(InstallGame(entry, assets.games.at("alpha"), pbox, error, true)),
               "retry after metadata failure succeeds");
    }

    // Cache write failure: the fetch still succeeds and the launcher keeps working.
    {
        h.AddGame("beta", "Beta", "1.0.0", "Studio B");
        h.PointAssetsUrlToServer();
        h.WriteLocalFile("assets.cache.json.tmp/dummy", "x");
        Assets fetched;
        expect(R_SUCCEEDED(FetchRemoteAssets(fetched, {})), "fetch succeeds without cache");
        expect(!fs::exists(h.Root() / "assets.cache.json"), "cache write failure keeps no cache");
        fs::remove_all(h.Root() / "assets.cache.json.tmp");
    }
}

// ---------------------------------------------------------------------------------------------
// I/J. Settings, About and base data prompts
// ---------------------------------------------------------------------------------------------

void test_settings_and_base(Harness& h) {
    std::cout << "[settings]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.WriteLocalCatalogue({"alpha"});
    h.PointAssetsUrlToServer();

    auto app = new_app();
    auto* menu = menu_of(*app);
    expect(menu != nullptr, "settings menu opens");
    if (menu == nullptr) {
        return;
    }

    press(*app, Button::SELECT);
    auto* settings = dynamic_cast<Sidebar*>(app->TestTop());
    expect(settings != nullptr, "settings without local catalogue");
    if (settings != nullptr) {
        expect_eq(settings->ItemCount(), size_t(5), "five settings entries");
        expect(!sidebar_has(*settings, "Clear local catalogue"), "no clear-local entry");
        press(*app, Button::B);
    }

    // About with ready base data.
    press(*app, Button::SELECT);
    select_sidebar(*app, "About");
    {
        const auto text = dialog_message(*app);
        expect(text.find("NSMBW Launcher vtest") == 0, "about version");
        expect(text.find("Installed games: 0") != std::string::npos, "about installed count");
        expect(text.find("Catalogue games: 1") != std::string::npos, "about catalogue count");
        expect(text.find("Base game data: Ready") != std::string::npos, "about ready base");
        expect(text.find("Launcher root: " + h.Root().string()) != std::string::npos,
               "about launcher root");
        expect(text.find("Credits: timschneeb, WitherBlitz, WiiCompiled, Aurora, Sphaira") !=
                   std::string::npos,
               "about credits");
        close_dialog(*app);
        press(*app, Button::B); // close Settings
    }

    // Clear local catalogue, cancel then confirm.
    h.WriteLocalFile("assets.local.json", h.LocalCatalogueJson({"alpha"}));
    press(*app, Button::SELECT);
    settings = dynamic_cast<Sidebar*>(app->TestTop());
    expect(settings != nullptr && sidebar_has(*settings, "Clear local catalogue"),
           "clear local catalogue appears");
    if (settings != nullptr) {
        expect(select_sidebar(*app, "Clear local catalogue"), "clear local selected");
        expect(dialog_message(*app).find("Clear the local catalogue?") == 0, "clear confirmation");
        close_dialog(*app);
        expect(fs::exists(h.Root() / "assets.local.json"), "clear cancel keeps the file");
    }
    press(*app, Button::SELECT);
    select_sidebar(*app, "Clear local catalogue");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Local catalogue cleared."), "clear message");
    expect(!fs::exists(h.Root() / "assets.local.json"), "clear removes the file");
    close_dialog(*app);
    press(*app, Button::SELECT);
    settings = dynamic_cast<Sidebar*>(app->TestTop());
    expect(settings != nullptr && !sidebar_has(*settings, "Clear local catalogue"),
           "clear entry disappears");
    if (settings != nullptr) {
        press(*app, Button::B);
    }

    // About with missing / dvd_root / incomplete base data.
    {
        test::SetBaseState(BaseDataState::Missing, {});
        auto scoped = new_app();
        if (dialog_of(*scoped) != nullptr) {
            close_dialog(*scoped); // dismiss the boot prompt
        }
        press(*scoped, Button::SELECT);
        select_sidebar(*scoped, "About");
        expect(dialog_message(*scoped).find("Base game data: Missing") != std::string::npos,
               "about missing base");
        close_dialog(*scoped);
        press(*scoped, Button::B);
        test::Reset();
    }
    {
        make_manual("manual", "2.5.0");
        test::SetBaseState(BaseDataState::Missing, {});
        test::g_base_ready_for = true;
        auto scoped = new_app();
        press(*scoped, Button::SELECT);
        select_sidebar(*scoped, "About");
        expect(dialog_message(*scoped).find("Base game data: Ready (dvd_root)") != std::string::npos,
               "about dvd_root base");
        close_dialog(*scoped);
        press(*scoped, Button::B);
        test::Reset();
    }
    {
        test::SetBaseState(BaseDataState::Incomplete, "Missing sys/main.dol.");
        auto scoped = new_app();
        if (dialog_of(*scoped) != nullptr) {
            close_dialog(*scoped); // dismiss the boot prompt
        }
        press(*scoped, Button::SELECT);
        select_sidebar(*scoped, "About");
        expect(dialog_message(*scoped).find("Base game data: Incomplete (Missing sys/main.dol.)") !=
                   std::string::npos,
               "about incomplete base");
        close_dialog(*scoped);
        press(*scoped, Button::B);
        test::Reset();
    }

    // Boot prompts.
    {
        test::SetBaseState(BaseDataState::Missing, {});
        auto scoped = new_app();
        const auto text = dialog_message(*scoped);
        expect(text.find("Base game data is missing") == 0, "boot missing base prompt");
        expect(text.find("disc image") != std::string::npos, "boot prompt explains the image");
        expect(text.find(RootDirectory()) != std::string::npos, "boot prompt shows the folder");
        close_dialog(*scoped);
        test::Reset();
    }
    {
        test::SetBaseState(BaseDataState::Incomplete, "Missing sys/main.dol.");
        auto scoped = new_app();
        const auto text = dialog_message(*scoped);
        expect(text.find("Base game data can't be used") == 0, "boot incomplete prompt");
        expect(text.find("Missing sys/main.dol.") != std::string::npos, "boot problem text");
        close_dialog(*scoped);
        test::Reset();
    }
    {
        test::SetBaseState(BaseDataState::Incomplete, "Missing sys/main.dol.");
        test::SetBaseImage((h.Root() / "game.iso").string());
        auto scoped = new_app();
        auto* prompt = dialog_of(*scoped);
        expect(prompt != nullptr && prompt->Message().find("Replace the base game data?") == 0,
               "boot re-extract prompt");
        close_dialog(*scoped); // cancel
        expect(test::g_base_state == BaseDataState::Incomplete, "re-extract cancel keeps state");

        scoped = new_app();
        confirm_dialog(*scoped);
        expect_eq(dialog_message(*scoped), std::string("Base game data ready."),
                  "re-extract success message");
        expect(test::g_base_state == BaseDataState::Ready, "re-extract makes base ready");
        close_dialog(*scoped);
        test::Reset();
    }
    {
        test::SetBaseState(BaseDataState::Missing, {});
        test::SetBaseImage((h.Root() / "game.iso").string());
        auto scoped = new_app();
        expect_eq(dialog_message(*scoped), std::string("Base game data ready."),
                  "automatic extraction message");
        close_dialog(*scoped);
        test::Reset();
    }
    {
        test::SetBaseState(BaseDataState::Incomplete, "Missing sys/main.dol.");
        test::SetBaseImage((h.Root() / "game.iso").string());
        auto scoped = new_app();
        launcher::ui::TestProgressCancelAfter() = 0;
        launcher::ui::TestProgressCancelCalls() = 0;
        confirm_dialog(*scoped); // start the re-extraction
        launcher::ui::TestProgressCancelAfter() = -1;
        launcher::ui::TestProgressCancelCalls() = 0;
        expect(dialog_of(*scoped) == nullptr, "cancelled extraction is silent");
        expect(test::g_base_state == BaseDataState::Incomplete,
               "cancelled extraction keeps the base state");
        test::Reset();
    }
    {
        test::SetBaseState(BaseDataState::Incomplete, "Missing sys/main.dol.");
        test::SetBaseImage((h.Root() / "game.iso").string());
        test::g_base_install_ok = false;
        test::g_base_install_error = "Could not extract the disc image: bad sector.";
        auto scoped = new_app();
        confirm_dialog(*scoped);
        expect_eq(dialog_message(*scoped), std::string("Could not extract the disc image: bad sector."),
                  "extraction failure message");
        close_dialog(*scoped);
        test::Reset();
    }
}

// ---------------------------------------------------------------------------------------------
// F/K. Free space and cancellation
// ---------------------------------------------------------------------------------------------

void test_space_and_cancel(Harness& h) {
    std::cout << "[space]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.WriteLocalCatalogue({"alpha"});

    Assets assets;
    LoadLocalAssets(assets);
    GameEntry entry;
    entry.id = "alpha";
    entry.name = "Alpha";
    entry.dir = (fs::path(GamesDirectory()) / "alpha").string();
    entry.nro_path = (fs::path(entry.dir) / "game.nro").string();
    entry.romfs_path = (fs::path(entry.dir) / "assets.romfs").string();

    // Prompt text differs between a fresh install and a current/forced install.
    {
        const auto fresh = InstallPromptMessage(entry, assets.games.at("alpha"), "Install");
        expect(fresh.find("Download") != std::string::npos, "fresh install prompt has a size");
    }

    // Free space read failure.
    test::g_sd_size = {true, 0, 0};
    {
        ui::ProgressBox pbox;
        std::string error;
        expect(R_FAILED(InstallGame(entry, assets.games.at("alpha"), pbox, error, false)),
               "sd size failure aborts install");
        expect_eq(error, std::string("Could not read the SD card free space."),
                  "sd size failure message");
        const auto prompt = InstallPromptMessage(entry, assets.games.at("alpha"), "Install");
        expect(prompt.find(", ") == std::string::npos, "prompt has no available space");
    }

    // Not enough space.
    test::g_sd_size = {false, 1024 * 1024, 8 * 1024 * 1024};
    {
        ui::ProgressBox pbox;
        std::string error;
        expect(R_FAILED(InstallGame(entry, assets.games.at("alpha"), pbox, error, false)),
               "insufficient space aborts install");
        expect(error.find("Not enough space.") == 0, "insufficient space message");
        expect(!fs::exists(entry.nro_path), "insufficient space writes nothing");
    }
    test::Reset();

    // Cancelled install through the UI is silent and leaves no files.
    {
        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu != nullptr) {
            select_row(*app, *menu, 0);
            launcher::ui::TestProgressCancelAfter() = 0;
            launcher::ui::TestProgressCancelCalls() = 0;
            press(*app, Button::A); // install row starts the install box
            expect(app->TestTop() == menu, "cancelled install shows no message");
            expect(!fs::exists(fs::path(GamesDirectory()) / "alpha" / "game.nro"),
                   "cancelled install leaves no game.nro");
            expect(!fs::exists(fs::path(GamesDirectory()) / "alpha" / "game.nro.part"),
                   "cancelled install leaves no part");
        }
    }
    test::Reset();
    launcher::ui::TestProgressCancelAfter() = -1;
    launcher::ui::TestProgressCancelCalls() = 0;

    // Cancelled verification reports cancellation to the caller.
    {
        install_local("alpha");
        const auto current = InstallPromptMessage(entry, assets.games.at("alpha"), "Update");
        expect(current.find("The game files are on the SD card.") != std::string::npos,
               "current update has no download size");
        const auto forced = InstallPromptMessage(entry, assets.games.at("alpha"), "Reinstall", true);
        expect(forced.find("Download") != std::string::npos, "force prompt has a download size");
        ui::ProgressBox pbox;
        std::string error;
        expect(R_SUCCEEDED(VerifyGame(entry, pbox, error)), "verify install");
        launcher::ui::TestProgressCancelAfter() = 0;
        launcher::ui::TestProgressCancelCalls() = 0;
        error.clear();
        expect(R_FAILED(VerifyGame(entry, pbox, error)), "cancelled verify fails");
        expect_eq(error, std::string("Install cancelled."), "verify cancellation message");
        launcher::ui::TestProgressCancelAfter() = -1;
        launcher::ui::TestProgressCancelCalls() = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// H. Launcher update
// ---------------------------------------------------------------------------------------------

void test_launcher_update(Harness& h) {
    std::cout << "[launcher]\n";

    h.PublishLauncher("1.0.0");
    test::WriteFakeNro(h.Root() / "launcher.nro", "1.0.0");
    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.WriteLocalCatalogue({"alpha"});
    h.PointAssetsUrlToServer();

    // No update in the catalogue.
    auto app = new_app();
    auto* menu = menu_of(*app);
    expect(menu != nullptr, "launcher menu opens");
    if (menu == nullptr) {
        return;
    }
    press(*app, Button::SELECT);
    select_sidebar(*app, "Check launcher update");
    expect_eq(dialog_message(*app), std::string("Launcher is up to date."),
              "launcher up to date");
    close_dialog(*app);

    // Publish a newer launcher and check the dialog.
    h.PublishLauncher("2.0.0", 'L');
    h.SetLauncher("2.0.0", h.Server().Url("/launcher.nro"));
    press(*app, Button::SELECT);
    select_sidebar(*app, "Check launcher update");
    {
        const auto text = dialog_message(*app);
        expect(text.find("Launcher update available") == 0, "launcher update dialog");
        expect(text.find("Version 2.0.0") != std::string::npos, "launcher version in dialog");
        close_dialog(*app); // cancel
    }
    {
        std::string version;
        expect(R_SUCCEEDED(ReadNroVersion((h.Root() / "launcher.nro").string(), version)),
               "cancelled launcher update keeps a valid launcher");
        expect_eq(version, std::string("1.0.0"), "cancelled launcher update keeps version");
    }

    // Confirm the update; the app relaunches the launcher.
    h.ResetHits();
    press(*app, Button::SELECT);
    select_sidebar(*app, "Check launcher update");
    confirm_dialog(*app);
    expect(app->IsQuitting(), "launcher update quits to relaunch");
    expect_eq(app->LaunchedPath(), (h.Root() / "launcher.nro").string(),
              "launcher update relaunches the launcher");
    {
        std::string version;
        expect(R_SUCCEEDED(ReadNroVersion((h.Root() / "launcher.nro").string(), version)),
               "updated launcher is a valid nro");
        expect_eq(version, std::string("2.0.0"), "updated launcher version");
        expect_eq(test::FileSha(h.Root() / "launcher.nro"), test::FileSha(h.Src() / "launcher.nro"),
                  "updated launcher content");
        expect(!fs::exists(h.Root() / "launcher.nro.new"), "no staging file left");
        expect(!fs::exists(h.Root() / "launcher.nro.old"), "no backup file left");
    }

    // The launcher row starts the same flow.
    h.PublishLauncher("2.1.0", 'M');
    h.SetLauncher("2.1.0", h.Server().Url("/launcher.nro"));
    h.WriteLocalCatalogue({"alpha"});
    {
        auto scoped = new_app();
        auto* scoped_menu = menu_of(*scoped);
        expect(scoped_menu != nullptr, "launcher-row menu opens");
        if (scoped_menu != nullptr) {
            expect_eq(scoped_menu->TestRowCount(), s64(2), "launcher row plus download row");
            expect(scoped_menu->TestRows().empty(), "no installed games");
            press(*scoped, Button::A); // launcher row is index 0
            expect(scoped->IsQuitting(), "launcher row update relaunches");
            std::string version;
            ReadNroVersion((h.Root() / "launcher.nro").string(), version);
            expect_eq(version, std::string("2.1.0"), "launcher row installs the new version");
        }
    }

    // Failure: bad SHA keeps the launcher.
    h.PublishLauncher("3.0.0", 'F');
    h.SetLauncher("3.0.0", h.Server().Url("/launcher.nro"), true);
    {
        auto scoped = new_app();
        press(*scoped, Button::SELECT);
        select_sidebar(*scoped, "Check launcher update");
        confirm_dialog(*scoped);
        expect(dialog_message(*scoped).find("Checksum mismatch for launcher.nro.") == 0,
               "launcher bad sha message");
        close_dialog(*scoped);
        std::string version;
        ReadNroVersion((h.Root() / "launcher.nro").string(), version);
        expect_eq(version, std::string("2.1.0"), "bad sha keeps the launcher");
        expect(!fs::exists(h.Root() / "launcher.nro.new.part"), "bad sha leaves no part");
        expect(!fs::exists(h.Root() / "launcher.nro.old"), "bad sha leaves no backup");
    }

    // Failure: wrong NRO version.
    h.PublishLauncher("3.9.0", 'V');
    h.SetLauncher("4.0.0", h.Server().Url("/launcher.nro"));
    {
        auto scoped = new_app();
        press(*scoped, Button::SELECT);
        select_sidebar(*scoped, "Check launcher update");
        confirm_dialog(*scoped);
        expect_eq(dialog_message(*scoped),
                  std::string("The download is version 3.9.0, expected 4.0.0."),
                  "launcher version mismatch message");
        close_dialog(*scoped);
        std::string version;
        ReadNroVersion((h.Root() / "launcher.nro").string(), version);
        expect_eq(version, std::string("2.1.0"), "version mismatch keeps the launcher");
    }

    // Failure: invalid NRO.
    test::WriteFile(h.Src() / "launcher.nro", "not an nro");
    h.SetLauncher("5.0.0", h.Server().Url("/launcher.nro"));
    {
        auto scoped = new_app();
        press(*scoped, Button::SELECT);
        select_sidebar(*scoped, "Check launcher update");
        confirm_dialog(*scoped);
        expect_eq(dialog_message(*scoped), std::string("The downloaded launcher is not an NRO."),
                  "invalid launcher message");
        close_dialog(*scoped);
    }

    // Cancelled launcher download leaves everything intact.
    h.PublishLauncher("6.0.0", 'C', 2 * 1024 * 1024);
    h.SetLauncher("6.0.0", h.Server().Url("/launcher.nro"));
    {
        auto scoped = new_app();
        press(*scoped, Button::SELECT);
        select_sidebar(*scoped, "Check launcher update");
        launcher::ui::TestProgressCancelAfter() = 0;
        launcher::ui::TestProgressCancelCalls() = 0;
        confirm_dialog(*scoped);
        launcher::ui::TestProgressCancelAfter() = -1;
        launcher::ui::TestProgressCancelCalls() = 0;
        expect(scoped->TestTop() == menu_of(*scoped), "cancelled launcher update is silent");
        std::string version;
        ReadNroVersion((h.Root() / "launcher.nro").string(), version);
        expect_eq(version, std::string("2.1.0"), "cancelled launcher update keeps version");
        expect(!fs::exists(h.Root() / "launcher.nro.new"), "cancelled update leaves no staging");
        expect(!fs::exists(h.Root() / "launcher.nro.new.part"), "cancelled update leaves no part");
    }

    // A launcher version without a URL is not offered as an update.
    h.SetLauncher("7.0.0", "");
    {
        auto scoped = new_app();
        press(*scoped, Button::SELECT);
        select_sidebar(*scoped, "Check launcher update");
        expect_eq(dialog_message(*scoped), std::string("Launcher is up to date."),
                  "launcher without a URL is ignored");
        close_dialog(*scoped);
    }
}

// ---------------------------------------------------------------------------------------------
// C. Main menu edge cases
// ---------------------------------------------------------------------------------------------

void test_main_menu_edges(Harness& h) {
    std::cout << "[main]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.SetRomfs("alpha", "1.0.0", std::string(1024, 'r'));
    h.WriteLocalCatalogue({"alpha"});
    h.PointAssetsUrlToServer();
    install_local("alpha");

    // Missing base data: warn, cancel, then launch anyway.
    test::g_base_ready = false;
    test::g_base_ready_for = false;
    {
        auto app = new_app();
        auto* menu = menu_of(*app);
        expect(menu != nullptr, "missing-base menu opens");
        if (menu == nullptr) {
            return;
        }
        select_row(*app, *menu, 0);
        expect(menu->m_actions.at(Button::A).m_hint == "Launch", "missing base hint");
        press(*app, Button::A);
        auto* prompt = dialog_of(*app);
        expect(prompt != nullptr && prompt->Message().find("Base game data is missing") == 0,
               "launch base prompt");
        expect(prompt != nullptr && prompt->Message().find(RootDirectory()) != std::string::npos,
               "launch base prompt shows the folder");
        close_dialog(*app); // cancel
        expect(!app->IsQuitting(), "base prompt cancel does not launch");

        press(*app, Button::A);
        confirm_dialog(*app);
        expect(app->IsQuitting(), "launch anyway launches");
        expect_eq(app->LaunchedPath(), (fs::path(GamesDirectory()) / "alpha" / "game.nro").string(),
                  "launch path");
        expect(test::ReadFile(h.Root() / "usage.json").find("\"alpha\"") != std::string::npos,
               "launch records usage");
    }
    test::Reset();

    // Missing base data adds a warning to the install prompt.
    {
        h.AddGame("beta", "Beta", "1.0.0", "Studio B");
        h.SetRomfs("beta", "1.0.0", std::string(64, 'b'));
        h.WriteLocalCatalogue({"alpha", "beta"});
        h.PointAssetsUrlToServer();
        test::g_base_ready = false;
        test::g_base_ready_for = false;
        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu != nullptr) {
            select_row(*app, *menu, menu->TestRowCount() - 1);
            press(*app, Button::A); // download page
            auto* download = dynamic_cast<DownloadMenu*>(app->TestTop());
            if (download != nullptr) {
                press(*app, Button::A); // first row
                const auto text = dialog_message(*app);
                expect(text.find("Base game data is missing; a USA Revision 1 (SMNE01) disc image is required.") !=
                           std::string::npos,
                       "install prompt warns about base data");
                close_dialog(*app);
            }
        }
    }
    test::Reset();

    // Invalid manual NRO.
    {
        const fs::path dir = make_manual("broken", "1.0.0");
        h.WriteLocalFile(dir / "game.nro", "broken");
        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu != nullptr) {
            const s64 index = row_index(*menu, "broken");
            expect(index >= 0, "invalid manual game listed");
            if (index >= 0) {
                select_row(*app, *menu, index);
                press(*app, Button::A);
                const auto text = dialog_message(*app);
                expect(text.find("Not a valid NRO") == 0, "invalid nro error");
                expect(!app->IsQuitting(), "invalid nro does not launch");
                close_dialog(*app);
            }
        }
    }

    // Manual installs: version from NACP, no update/reinstall entries.
    {
        make_manual("manual", "2.5.0");
        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu != nullptr) {
            const s64 index = row_index(*menu, "manual");
            expect(index >= 0, "manual install listed");
            if (index >= 0) {
                expect_eq(menu->TestRows()[static_cast<size_t>(index)].version,
                          std::string("2.5.0"), "manual version from NACP");
                select_row(*app, *menu, index);
                press(*app, Button::X);
                auto* sidebar = dynamic_cast<Sidebar*>(app->TestTop());
                expect(sidebar != nullptr, "manual options open");
                if (sidebar != nullptr) {
                    expect(!sidebar_has(*sidebar, "Check for updates"), "manual has no update check");
                    expect(!sidebar_has(*sidebar, "Reinstall"), "manual has no reinstall");
                    expect(sidebar_has(*sidebar, "Verify files"), "manual verify entry");
                    press(*app, Button::B);
                }
                press(*app, Button::A);
                expect(app->IsQuitting(), "manual launch");
                expect_eq(app->LaunchedPath(), (fs::path(GamesDirectory()) / "manual" / "game.nro").string(),
                          "manual launch path");
            }
        }
    }

    // Sort by last use, ignore hidden and removed folders, tolerate bad metadata.
    {
        h.WriteLocalCatalogue({"alpha"});
        TouchUsage("manual");
        make_manual(".hidden", "1.0.0");
        make_manual("trash.removed", "1.0.0");
        h.AddGame("legacy", "Legacy", "1.0.0", "Old");
        h.WriteLocalFile("games/legacy/installed.json",
                         "{\"name\":\"Legacy\",\"components\":{\"nro\":\"1.5.0\"}}");
        test::WriteFakeNro(fs::path(GamesDirectory()) / "legacy" / "game.nro", "1.5.0");
        h.WriteLocalFile("games/corrupt/installed.json", "{bad");
        test::WriteFakeNro(fs::path(GamesDirectory()) / "corrupt" / "game.nro", "3.0.0");

        auto assets = Assets{};
        expect(R_SUCCEEDED(LoadLocalAssets(assets)), "edge catalogue loads");
        const auto all = ScanGames(assets);
        const auto find = [&all](const std::string& id) -> const GameEntry* {
            for (const auto& entry : all) {
                if (entry.id == id) {
                    return &entry;
                }
            }
            return nullptr;
        };
        expect(find(".hidden") == nullptr, "hidden folder ignored");
        expect(find("trash.removed") == nullptr, "removed folder ignored");
        const auto* legacy = find("legacy");
        expect(legacy != nullptr && legacy->version == "1.5.0" && !legacy->manual,
               "legacy string component parsed");
        const auto* corrupt = find("corrupt");
        expect(corrupt != nullptr && corrupt->manual, "corrupt metadata treated as manual");

        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu != nullptr) {
            // Manual has the newest usage entry, so it sorts first.
            expect_eq(menu->TestRows()[0].id, std::string("manual"), "last used sorts first");
        }
    }
}

// ---------------------------------------------------------------------------------------------
// E. Delete, verify and catalogue removal
// ---------------------------------------------------------------------------------------------

void select_download_row(App& app, DownloadMenu& menu, const std::string& id) {
    s64 target = -1;
    for (size_t i = 0; i < menu.TestRows().size(); ++i) {
        if (menu.TestRows()[i].id == id) {
            target = static_cast<s64>(i);
            break;
        }
    }
    expect(target >= 0, "download row exists: " + id);
    if (target < 0) {
        return;
    }
    for (int guard = 0; guard < 64 && menu.TestIndex() > target; ++guard) {
        press(app, Button::UP);
    }
    for (int guard = 0; guard < 64 && menu.TestIndex() < target; ++guard) {
        press(app, Button::DOWN);
    }
}

void test_remove_and_verify(Harness& h) {
    std::cout << "[remove]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.SetRomfs("alpha", "1.0.0", std::string(2048, 'r'));
    h.WriteLocalCatalogue({"alpha"});
    h.PointAssetsUrlToServer();
    install_local("alpha");

    const fs::path dir = fs::path(GamesDirectory()) / "alpha";
    test::WriteFile(dir / "data" / "save.txt", "save");
    TouchUsage("alpha");

    auto app = new_app();
    auto* menu = menu_of(*app);
    expect(menu != nullptr, "remove menu opens");
    if (menu == nullptr) {
        return;
    }

    // Verify files reports success.
    select_row(*app, *menu, "alpha");
    press(*app, Button::X);
    expect(select_sidebar(*app, "Verify files"), "verify selected");
    expect_eq(dialog_message(*app), std::string("Files verified for Alpha."), "verify success");
    close_dialog(*app);

    // Cancelling Delete game keeps everything.
    press(*app, Button::X);
    expect(select_sidebar(*app, "Delete game"), "delete selected");
    {
        const auto text = dialog_message(*app);
        expect(text.find("Delete Alpha?") == 0, "delete game confirmation");
        expect(text.find("game files are removed") != std::string::npos, "delete game explanation");
        close_dialog(*app); // cancel
    }
    press(*app, Button::B); // close the sidebar, which stays open on cancel
    expect(fs::is_regular_file(dir / "game.nro"), "cancelled delete keeps game.nro");
    expect(fs::is_regular_file(dir / "installed.json"), "cancelled delete keeps metadata");

    // Delete game keeps saves and removes the install.
    press(*app, Button::X);
    select_sidebar(*app, "Delete game");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Deleted Alpha."), "delete game message");
    close_dialog(*app);
    expect(!fs::exists(dir / "game.nro"), "delete game removes game.nro");
    expect(!fs::exists(dir / "assets.romfs"), "delete game removes romfs");
    expect(!fs::exists(dir / "installed.json"), "delete game removes metadata");
    expect(fs::is_regular_file(dir / "data" / "save.txt"), "delete game keeps save data");
    expect(test::ReadFile(h.Root() / "usage.json").find("\"alpha\"") == std::string::npos,
           "delete game removes usage");
    expect(menu->TestRows().empty(), "deleted game leaves the main list");
    expect_eq(menu->m_download_count, s64(1), "deleted game returns to the download count");

    // It can be installed again from the download page.
    select_row(*app, *menu, menu->TestRowCount() - 1);
    press(*app, Button::A);
    auto* download = dynamic_cast<DownloadMenu*>(app->TestTop());
    expect(download != nullptr && download->TestRows().size() == 1 &&
               download->TestRows()[0].id == "alpha",
           "deleted game returns to the download page");
    if (download != nullptr) {
        press(*app, Button::A);
        confirm_dialog(*app);
        expect_eq(dialog_message(*app), std::string("Installed Alpha."), "reinstall after delete");
        close_dialog(*app);
        press(*app, Button::B);
    }

    // Delete game and save data removes the whole folder.
    select_row(*app, *menu, "alpha");
    press(*app, Button::X);
    select_sidebar(*app, "Delete game and save data");
    expect(dialog_message(*app).find("This cannot be undone.") != std::string::npos,
           "delete and save warning");
    confirm_dialog(*app);
    expect_eq(dialog_message(*app), std::string("Deleted Alpha and save data."),
              "delete and save message");
    close_dialog(*app);
    expect(!fs::exists(dir), "delete and save removes the folder");

    // Verify failure modes.
    install_local("alpha");
    fs::remove(dir / "assets.romfs");
    {
        auto scoped = new_app();
        auto* scoped_menu = menu_of(*scoped);
        if (scoped_menu != nullptr) {
            select_row(*scoped, *scoped_menu, "alpha");
            press(*scoped, Button::X);
            select_sidebar(*scoped, "Verify files");
            expect_eq(dialog_message(*scoped), std::string("Missing assets.romfs."),
                      "verify missing romfs");
            close_dialog(*scoped);
        }
    }

    install_local("alpha");
    {
        std::ofstream file(dir / "game.nro", std::ios::binary | std::ios::app);
        file.put('\0');
    }
    {
        auto scoped = new_app();
        auto* scoped_menu = menu_of(*scoped);
        if (scoped_menu != nullptr) {
            select_row(*scoped, *scoped_menu, "alpha");
            press(*scoped, Button::X);
            select_sidebar(*scoped, "Verify files");
            expect_eq(dialog_message(*scoped), std::string("Size mismatch for game.nro."),
                      "verify size mismatch");
            close_dialog(*scoped);
        }
    }

    fs::remove(dir / "installed.json");
    {
        auto scoped = new_app();
        auto* scoped_menu = menu_of(*scoped);
        if (scoped_menu != nullptr) {
            select_row(*scoped, *scoped_menu, "alpha");
            press(*scoped, Button::X);
            select_sidebar(*scoped, "Verify files");
            expect_eq(dialog_message(*scoped),
                      std::string("There is no install metadata to verify."),
                      "verify without metadata");
            close_dialog(*scoped);
        }
    }

    test::WriteFile(dir / "installed.json",
              "{\"name\":\"Alpha\",\"components\":{\"nro\":{\"version\":\"1.0.0\"}}}");
    {
        auto scoped = new_app();
        auto* scoped_menu = menu_of(*scoped);
        if (scoped_menu != nullptr) {
            select_row(*scoped, *scoped_menu, "alpha");
            press(*scoped, Button::X);
            select_sidebar(*scoped, "Verify files");
            expect_eq(dialog_message(*scoped),
                      std::string("No checksums are recorded. Reinstall the game first."),
                      "verify without checksums");
            close_dialog(*scoped);
        }
    }

    // A catalogued manual install gains checksums when it is reinstalled.
    h.AddGame("manualcat", "Manual Cat", "1.0.0", "Manual Studio");
    fs::create_directories(fs::path(GamesDirectory()) / "manualcat");
    fs::copy_file(h.Src() / "manualcat.nro", fs::path(GamesDirectory()) / "manualcat" / "game.nro",
                  fs::copy_options::overwrite_existing);
    h.WriteLocalCatalogue({"manualcat"});
    {
        auto scoped = new_app();
        auto* scoped_menu = menu_of(*scoped);
        if (scoped_menu != nullptr) {
            select_row(*scoped, *scoped_menu, "manualcat");
            expect(scoped_menu->TestRows()[row_index(*scoped_menu, "manualcat")].manual,
                   "manual install flagged");
            press(*scoped, Button::X);
            select_sidebar(*scoped, "Reinstall");
            confirm_dialog(*scoped);
            expect_eq(dialog_message(*scoped), std::string("Installed Manual Cat."),
                      "manual reinstall message");
            close_dialog(*scoped);
            const Installed installed =
                ReadInstalled((fs::path(GamesDirectory()) / "manualcat").string());
            expect(installed.present && !installed.nro.sha256.empty(),
                   "manual reinstall records checksums");
        }
    }

    // A game removed from the catalogue during an update check.
    h.AddGame("beta", "Beta", "1.0.0", "Studio B");
    h.WriteLocalCatalogue({"alpha", "manualcat"});
    install_local("alpha"); // restore the corrupted test install
    h.RemoveGame("alpha");
    {
        auto scoped = new_app();
        auto* scoped_menu = menu_of(*scoped);
        if (scoped_menu != nullptr) {
            select_row(*scoped, *scoped_menu, "alpha");
            press(*scoped, Button::X);
            select_sidebar(*scoped, "Check for updates");
            expect_eq(dialog_message(*scoped),
                      std::string("This game is no longer in the catalogue."),
                      "catalogue removal message");
            close_dialog(*scoped);
            press(*scoped, Button::X);
            auto* sidebar = dynamic_cast<Sidebar*>(scoped->TestTop());
            expect(sidebar != nullptr, "unmanaged game options open");
            if (sidebar != nullptr) {
                expect(!sidebar_has(*sidebar, "Check for updates"), "unmanaged hides update check");
                expect(!sidebar_has(*sidebar, "Reinstall"), "unmanaged hides reinstall");
                expect(sidebar_has(*sidebar, "Verify files"), "unmanaged keeps verify");
                expect(sidebar_has(*sidebar, "Delete game"), "unmanaged keeps delete");
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// D. Download page failures
// ---------------------------------------------------------------------------------------------

void test_download_errors(Harness& h) {
    std::cout << "[download errors]\n";

    h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
    h.WriteLocalCatalogue({"alpha"});

    // A catalogue fetch failure shows an error and returns to the menu.
    h.PointAssetsUrl(h.Server().Url("/missing"));
    {
        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu == nullptr) {
            return;
        }
        expect_eq(menu->TestRowCount(), s64(1), "download row only");
        press(*app, Button::A);
        expect_eq(dialog_message(*app),
                  std::string("Could not load the catalogue. Check the network connection."),
                  "download catalogue error");
        close_dialog(*app);
        expect(app->TestTop() == menu, "error returns to the main menu");
        expect_eq(menu->m_download_count, s64(1), "offline catalogue keeps the local count");
    }

    // A cancelled fetch closes the page silently.
    h.Server().Route("/slow-catalogue", TestResponse{200, std::string(256 * 1024, 'c'),
                                                     "application/json", "", false, false, 1,
                                                     4096});
    h.PointAssetsUrl(h.Server().Url("/slow-catalogue"));
    {
        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu != nullptr) {
            launcher::ui::TestProgressCancelAfter() = 0;
            launcher::ui::TestProgressCancelCalls() = 0;
            press(*app, Button::A);
            launcher::ui::TestProgressCancelAfter() = -1;
            launcher::ui::TestProgressCancelCalls() = 0;
            expect(app->TestTop() == menu, "cancelled fetch returns silently");
            expect(dialog_of(*app) == nullptr, "cancelled fetch shows no dialog");
        }
    }

    // A catalogue with no usable games reports an error.
    h.Server().Route("/empty", TestResponse{200,
        "{\"schema\":1,\"launcher\":{\"version\":\"1.0.0\",\"url\":\"\"},\"games\":{}}",
        "application/json"});
    h.PointAssetsUrl(h.Server().Url("/empty"));
    {
        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu != nullptr) {
            press(*app, Button::A);
            expect_eq(dialog_message(*app),
                      std::string("Could not load the catalogue. Check the network connection."),
                      "empty catalogue error");
            close_dialog(*app);
            expect(app->TestTop() == menu, "empty catalogue returns to the menu");
        }
    }

    // The page always uses the network, even with a local catalogue.
    h.WriteLocalFile("assets.local.json", h.LocalCatalogueJson({"alpha"}));
    h.AddGame("beta", "Beta", "1.0.0", "Studio B");
    h.PointAssetsUrlToServer();
    {
        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu != nullptr) {
            press(*app, Button::A);
            auto* download = dynamic_cast<DownloadMenu*>(app->TestTop());
            expect(download != nullptr && download->TestRows().size() == 2,
                   "remote catalogue wins over assets.local.json");
            press(*app, Button::B);
        }
    }

    // A failed game download is reported with no partial files.
    h.AddGame("broken", "Broken", "1.0.0", "Studio C");
    h.SetNroUrlOverride("broken", "/missing-game");
    {
        auto app = new_app();
        auto* menu = menu_of(*app);
        if (menu != nullptr) {
            press(*app, Button::A); // download page, fetches the new catalogue
            auto* download = dynamic_cast<DownloadMenu*>(app->TestTop());
            expect(download != nullptr, "download page opens with broken game");
            if (download != nullptr) {
                select_download_row(*app, *download, "broken");
                press(*app, Button::A);
                confirm_dialog(*app);
                expect_eq(dialog_message(*app), std::string("Download failed for game.nro."),
                          "download failure message");
                close_dialog(*app);
                expect(!fs::exists(fs::path(GamesDirectory()) / "broken" / "game.nro"),
                       "failed download leaves no game.nro");
                expect(!fs::exists(fs::path(GamesDirectory()) / "broken" / "game.nro.part"),
                       "failed download leaves no part");
            }
        }
    }
}

} // namespace

int main() {
    std::cout << "launcher scenario tests\n";

    {
        Harness h("catalogue");
        test_catalogue(h);
    }
    {
        Harness h("download");
        test_download_and_install(h);
    }
    {
        Harness h("updates");
        test_updates(h);
    }
    {
        Harness h("transaction");
        test_transactional_update(h);
    }
    {
        Harness h("update-all");
        test_update_all(h);
    }
    {
        Harness h("remove");
        test_remove_and_verify(h);
    }
    {
        Harness h("download-errors");
        test_download_errors(h);
    }
    {
        Harness h("cleanup");
        test_cleanup_and_io(h);
    }
    {
        Harness h("settings");
        test_settings_and_base(h);
    }
    {
        Harness h("space");
        test_space_and_cancel(h);
    }
    {
        Harness h("launcher");
        test_launcher_update(h);
    }
    {
        Harness h("main");
        test_main_menu_edges(h);
    }
    {
        Harness h("space root");
        h.AddGame("alpha", "Alpha", "1.0.0", "Studio A");
        h.WriteLocalCatalogue({"alpha"});
        install_local("alpha");
        expect(fs::is_regular_file(fs::path(GamesDirectory()) / "alpha" / "game.nro"),
               "install works with spaces in the root path");
    }

    // No scenario may leave staging or partial files behind.
    {
        const fs::path base = "/tmp/opencode/launcher-scenarios";
        std::error_code ec;
        for (fs::recursive_directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) {
                continue;
            }
            const std::string name = it->path().filename().string();
            const bool leftover = name.ends_with(".part") || name.ends_with(".new") ||
                                  name.ends_with(".old") || name.ends_with(".importing");
            expect(!leftover, "no leftover staging file: " + it->path().string());
        }
    }

    if (g_failures != 0) {
        std::cerr << g_failures << " of " << g_checks << " scenario check(s) failed\n";
        return 1;
    }
    std::cout << "launcher scenario tests passed (" << g_checks << " checks)\n";
    return 0;
}
