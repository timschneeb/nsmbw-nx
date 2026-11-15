#include "demo_fixtures.hpp"

#include "test_fixtures.hpp"

#include <fstream>

namespace launcher::test {
namespace fs = std::filesystem;

auto CreateDemoData(const fs::path& root, const std::string& banner_source) -> DemoData {
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "games" / "test", ec);
    fs::create_directories(root / "games" / "newer", ec);
    fs::create_directories(root / "banners", ec);
    fs::create_directories(root / "src", ec);

    DemoData demo;
    demo.root = root;
    demo.test_nro = root / "src" / "test.nro";
    demo.newer_nro = root / "src" / "newer.nro";
    demo.extra_nro = root / "src" / "extra.nro";
    demo.launcher_nro = root / "src" / "launcher.nro";
    demo.newer_banner = root / "banners" / "newer.png";

    WriteFakeNro(demo.test_nro, "1.0.0");
    WriteFakeNro(demo.newer_nro, "0.9.0");
    WriteFakeNro(demo.extra_nro, "1.0.0");
    WriteFakeNro(demo.launcher_nro, "2.0.0");
    WriteFakeNro(root / "launcher.nro", "1.0.0");
    fs::copy_file(demo.test_nro, root / "games" / "test" / "game.nro");
    fs::copy_file(demo.newer_nro, root / "games" / "newer" / "game.nro");
    fs::copy_file(banner_source, demo.newer_banner, fs::copy_options::overwrite_existing);

    const std::string catalog =
        GameJson("test", "Test Game", "Test Author", demo.test_nro);
    const std::string newer = GameJson("newer", "Newer Game", "Newer Team", demo.newer_nro,
                                       "1.0.0", demo.newer_banner.string());
    const std::string extra =
        GameJson("extra", "Extra Game", "Extra Studio", demo.extra_nro);
    demo.launcher_url = "file://" + demo.launcher_nro.string();

    WriteCatalogue(root / "assets.json", {catalog, newer, extra}, "2.0.0", demo.launcher_url,
                   demo.launcher_nro, "0.0.1");
    WriteCatalogue(root / "remote-assets.json", {catalog, newer, extra}, "2.0.0",
                   demo.launcher_url, demo.launcher_nro, "0.0.1");
    std::ofstream(root / "assets.url") << "file://" << (root / "remote-assets.json").string()
                                       << "\n";
    return demo;
}

} // namespace launcher::test
