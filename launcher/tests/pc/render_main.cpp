#include "app.hpp"
#include "demo_fixtures.hpp"
#include "menu_main.hpp"
#include "paths.hpp"
#include "test_fixtures.hpp"
#include "ui/menus/menu_download.hpp"
#include "ui/sidebar.hpp"

#include "switch.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace launcher;

namespace {

void press(App& app, Button button) {
    Controller controller;
    controller.m_kdown = static_cast<u64>(button);
    TouchInfo touch;
    app.TestUpdateTop(&controller, &touch);
}

void shot(App& app, const std::string& name) {
    const std::string path = "/tmp/opencode/launcher-screenshots/" + name + ".png";
    if (!app.RenderScreenshot(path)) {
        std::cerr << "FAILED screenshot " << path << "\n";
    } else {
        std::cout << path << "\n";
    }
}

auto main_menu(App& app) -> ui::MainMenu* {
    return dynamic_cast<ui::MainMenu*>(app.TestWidgetAt(0));
}

void select_main(App& app, s64 index) {
    auto* menu = main_menu(app);
    if (menu == nullptr) {
        return;
    }
    for (int guard = 0; guard < 32 && menu->TestIndex() > index; ++guard) {
        press(app, Button::UP);
    }
    for (int guard = 0; guard < 32 && menu->TestIndex() < index; ++guard) {
        press(app, Button::DOWN);
    }
}

auto select_sidebar(App& app, const std::string& title) -> bool {
    auto* sidebar = dynamic_cast<ui::Sidebar*>(app.TestTop());
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
    for (int guard = 0; guard < 32 && sidebar->TestIndex() > target; ++guard) {
        press(app, Button::UP);
    }
    for (int guard = 0; guard < 32 && sidebar->TestIndex() < target; ++guard) {
        press(app, Button::DOWN);
    }
    press(app, Button::A);
    return true;
}

} // namespace

int main() {
    const fs::path root = "/tmp/opencode/launcher-render";
    SetRootDirectory((root / "launcher.nro").string().c_str());
    const auto demo = test::CreateDemoData(
        root, LAUNCHER_SOURCE_DIR "/launcher/assets/banners/newersmbw.png");

    App app;
    if (App::GetVg() == nullptr) {
        std::cerr << "render: no graphics context\n";
        return 1;
    }

    app.Push(std::make_unique<ui::MainMenu>());
    shot(app, "main");

    // Stale game options and the update prompt.
    press(app, Button::X);
    shot(app, "game-options");

    press(app, Button::A);
    shot(app, "update-prompt");
    press(app, Button::A); // cancel
    press(app, Button::B); // close options

    // Current game options.
    select_main(app, 1);
    press(app, Button::X);
    shot(app, "game-options-current");
    press(app, Button::B);

    // Download page and install confirmation.
    select_main(app, 3);
    press(app, Button::A);
    shot(app, "download");
    press(app, Button::A);
    shot(app, "install-confirm");
    press(app, Button::A); // cancel
    press(app, Button::B); // back to games

    // Settings and update-all confirmation.
    press(app, Button::SELECT);
    shot(app, "settings");
    press(app, Button::A); // Update all, fetches the catalogue
    shot(app, "update-all-confirm");
    press(app, Button::A); // cancel

    // About.
    press(app, Button::SELECT);
    press(app, Button::DOWN);
    press(app, Button::DOWN);
    press(app, Button::A);
    shot(app, "about");
    press(app, Button::B);
    press(app, Button::B);

    // Settings with a local catalogue.
    {
        std::ifstream source(root / "assets.json");
        std::ofstream target(root / "assets.local.json");
        target << source.rdbuf();
    }
    press(app, Button::SELECT);
    shot(app, "settings-local");
    press(app, Button::B);

    // Delete confirmation.
    select_main(app, 1);
    press(app, Button::X);
    select_sidebar(app, "Delete game");
    shot(app, "delete-confirm");
    press(app, Button::A); // cancel
    press(app, Button::B); // close options

    // Launcher update dialog.
    press(app, Button::SELECT);
    select_sidebar(app, "Check launcher update");
    shot(app, "launcher-dialog");
    press(app, Button::A); // cancel

    // All games installed: the download page shows its empty state.
    const std::string catalog =
        test::GameJson("test", "Test Game", "Test Author", demo.test_nro);
    const std::string newer = test::GameJson("newer", "Newer Game", "Newer Team",
                                             demo.newer_nro, "1.0.0",
                                             demo.newer_banner.string());
    test::WriteCatalogue(root / "remote-assets.json", {catalog, newer}, "2.0.0",
                         demo.launcher_url, demo.launcher_nro);
    select_main(app, 3);
    press(app, Button::A);
    shot(app, "download-empty");
    press(app, Button::B);

    // Catalogue error dialog.
    std::ofstream(root / "assets.url") << "file://" << (root / "does-not-exist.json").string() << "\n";
    select_main(app, 3);
    press(app, Button::A);
    shot(app, "download-error");

    return 0;
}
