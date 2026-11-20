#include "app.hpp"
#include "menu_main.hpp"
#include "paths.hpp"
#include "sha.hpp"
#include "test_fixtures.hpp"
#include "ui/option_box.hpp"
#include "ui/popup_list.hpp"
#include "ui/sidebar.hpp"

#include "switch.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace launcher;
using launcher::ui::OptionBox;
using launcher::ui::PopupList;
using launcher::ui::Sidebar;

namespace {

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        ++g_failures;
    }
}

void press(App& app, Button button) {
    Controller controller;
    controller.m_kdown = static_cast<u64>(button);
    TouchInfo touch;
    app.TestUpdateTop(&controller, &touch);
}

auto sidebar_item(const Sidebar& sidebar, const std::string& title) -> bool {
    for (size_t i = 0; i < sidebar.ItemCount(); ++i) {
        if (sidebar.ItemTitle(i) == title) {
            return true;
        }
    }
    return false;
}

auto select_sidebar(App& app, Sidebar& sidebar, const std::string& title) -> bool {
    s64 target = -1;
    for (size_t i = 0; i < sidebar.ItemCount(); ++i) {
        if (sidebar.ItemTitle(i) == title) {
            target = static_cast<s64>(i);
            break;
        }
    }
    if (target < 0) {
        return false;
    }
    for (int guard = 0; guard < 32 && sidebar.TestIndex() > target; ++guard) {
        press(app, Button::UP);
    }
    for (int guard = 0; guard < 32 && sidebar.TestIndex() < target; ++guard) {
        press(app, Button::DOWN);
    }
    press(app, Button::A);
    return true;
}

auto select_popup(App& app, PopupList& popup, s64 index) -> bool {
    for (int guard = 0; guard < 32 && popup.TestIndex() > index; ++guard) {
        press(app, Button::UP);
    }
    for (int guard = 0; guard < 32 && popup.TestIndex() < index; ++guard) {
        press(app, Button::DOWN);
    }
    press(app, Button::A);
    return true;
}

auto message(App& app) -> std::string {
    if (auto* box = dynamic_cast<OptionBox*>(app.TestTop())) {
        return box->Message();
    }
    return {};
}

} // namespace

int main() {
    const fs::path root = "/tmp/opencode/launcher-ui-tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "games" / "test");
    fs::create_directories(root / "games" / "newer");
    fs::create_directories(root / "src");

    SetRootDirectory((root / "launcher.nro").string().c_str());

    const fs::path test_nro = root / "src" / "test.nro";
    const fs::path newer_nro = root / "src" / "newer.nro";
    test::WriteFakeNro(test_nro, "1.0.0");
    test::WriteFakeNro(newer_nro, "0.9.0");
    fs::copy_file(test_nro, root / "games" / "test" / "game.nro");
    fs::copy_file(newer_nro, root / "games" / "newer" / "game.nro");

    test::WriteCatalogue(root / "assets.json", {
        test::GameJson("test", "Test Game", "Test Author", test_nro),
        test::GameJson("newer", "Newer Game", "Newer Team", newer_nro),
    }, "1.0.0", "", {}, "0.0.1");
    fs::copy_file(root / "assets.json", root / "remote-assets.json", fs::copy_options::overwrite_existing);
    std::ofstream(root / "assets.url") << "file://" << (root / "remote-assets.json").string() << "\n";

    App app;
    app.Push(std::make_unique<ui::MainMenu>());
    auto* menu = dynamic_cast<ui::MainMenu*>(app.TestTop());
    expect(menu != nullptr, "main menu is top");
    if (menu == nullptr) {
        return 1;
    }

    expect(menu->TestRows().size() == 2, "two games listed");
    const auto find_row = [&](const std::string& id) -> const GameEntry* {
        for (const auto& entry : menu->TestRows()) {
            if (entry.id == id) return &entry;
        }
        return nullptr;
    };
    const auto* test_row = find_row("test");
    const auto* newer_row = find_row("newer");
    expect(test_row != nullptr && test_row->author == "Test Author", "test author");
    expect(newer_row != nullptr && newer_row->author == "Newer Team", "newer author");
    expect(newer_row != nullptr && newer_row->state == GameState::Update, "newer has an update");

    expect(menu->m_actions.at(Button::X).m_hint == "Game options", "X is Game options");
    expect(menu->m_actions.at(Button::SELECT).m_hint == "Settings", "minus is Settings");

    // Move to the download row; X must disappear.
    press(app, Button::DOWN);
    press(app, Button::DOWN);
    expect(menu->TestIndex() == 2, "moved to download row");
    expect(menu->m_actions.at(Button::X).m_hint.empty(), "X hidden on download row");

    press(app, Button::UP);
    press(app, Button::UP);
    expect(menu->TestIndex() == 0, "back on a game row");

    // The Newer row is index 0 (name order) and is known to be stale.
    press(app, Button::X);
    auto* sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    expect(sidebar != nullptr, "game options sidebar");
    if (sidebar != nullptr) {
        expect(sidebar_item(*sidebar, "Update"), "stale game shows Update");
        expect(!sidebar_item(*sidebar, "Check for updates"), "stale game hides Check for updates");
        expect(sidebar_item(*sidebar, "Reinstall"), "reinstall entry");
        expect(sidebar_item(*sidebar, "Verify files"), "verify entry");
        expect(sidebar_item(*sidebar, "Delete game"), "delete entry");
        expect(sidebar_item(*sidebar, "Delete game and save data"), "delete and save entry");
    }
    press(app, Button::B);

    // The Test row is current and shows Check for updates.
    press(app, Button::DOWN);
    press(app, Button::X);
    sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    expect(sidebar != nullptr, "current game options sidebar");
    if (sidebar != nullptr) {
        expect(sidebar_item(*sidebar, "Check for updates"), "current game checks for updates");
        expect(!sidebar_item(*sidebar, "Update"), "current game hides Update");
    }
    press(app, Button::B);

    // Settings has no Base game data or cached-catalogue entry.
    press(app, Button::SELECT);
    sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    expect(sidebar != nullptr, "settings sidebar");
    if (sidebar != nullptr) {
        expect(sidebar->Title() == "Settings", "settings title");
        expect(sidebar_item(*sidebar, "Update all"), "update all entry");
        expect(sidebar_item(*sidebar, "Check launcher update"), "check launcher entry");
        expect(sidebar_item(*sidebar, "Display style"), "display style entry");
        expect(sidebar_item(*sidebar, "Download row"), "download row entry");
        expect(sidebar_item(*sidebar, "About"), "about entry");
        expect(!sidebar_item(*sidebar, "Base game data"), "base data entry removed");
        expect(!sidebar_item(*sidebar, "Clear cached catalogue"), "cached entry removed");
    }

    // About content.
    expect(select_sidebar(app, *sidebar, "About"), "about selected");
    {
        const auto text = message(app);
        expect(text.find("Credits: timschneeb, WitherBlitz, WiiCompiled, Aurora, Sphaira") != std::string::npos,
               "about credits");
        expect(text.find("Recomp version: 0.0.1") != std::string::npos, "about recomp version");
        expect(text.find("Installed games: 2") != std::string::npos, "about installed count");
        expect(text.find("Available games") == std::string::npos, "about has no available games");
        expect(text.find("Base game data: Ready") != std::string::npos, "about base data");
    }
    press(app, Button::B);

    // Update all refreshes the catalogue and reports through a dialog.
    expect(select_sidebar(app, *sidebar, "Update all"), "update all selected");
    {
        const auto text = message(app);
        expect(text.find("Update 1 game?") != std::string::npos,
               "update all confirmation dialog");
    }
    press(app, Button::B);

    // Check for updates on a current game reports through a dialog.
    press(app, Button::X);
    sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    if (sidebar != nullptr) {
        expect(sidebar_item(*sidebar, "Check for updates"), "check entry present");
        press(app, Button::A);
        const auto text = message(app);
        expect(text.find("is up to date.") != std::string::npos, "game update check dialog");
        press(app, Button::B);
    }

    // The stale game offers the update dialog, not a check result.
    if (menu->TestIndex() != 0) {
        press(app, Button::UP);
    }
    press(app, Button::X);
    sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    if (sidebar != nullptr && sidebar_item(*sidebar, "Update")) {
        press(app, Button::A);
        const auto text = message(app);
        expect(text.find("Update") == 0, "update prompt shown");
        press(app, Button::B);
    }

    // Display styles and the optional Download row.
    press(app, Button::B);
    press(app, Button::SELECT);
    sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    expect(sidebar != nullptr && sidebar_item(*sidebar, "Display style"),
           "display style entry");
    expect(sidebar != nullptr && sidebar_item(*sidebar, "Download row"),
           "download row entry");
    expect(sidebar != nullptr && select_sidebar(app, *sidebar, "Display style"),
           "display style selected");
    auto* popup = dynamic_cast<PopupList*>(app.TestTop());
    expect(popup != nullptr, "display style popup");
    if (popup != nullptr) {
        expect(popup->ItemCount() == 3, "three display styles");
        expect(select_popup(app, *popup, 1), "banner grid selected");
    }
    expect(menu->TestDisplayStyle() == DisplayStyle::BannerGrid, "banner grid applied");
    expect(menu->m_list->GetRow() == 3, "banner grid columns");
    expect(menu->m_list->GetPage() == 6, "banner grid page size");
    expect(menu->TestRowCount() == 3, "download row still counted");
    expect(test::ReadFile(root / "settings.json").find("\"banner_grid\"") != std::string::npos,
           "banner grid saved");

    // The settings sidebar stays open after the popup closes.
    sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    expect(sidebar != nullptr, "settings after banner grid");
    if (sidebar != nullptr) {
        expect(select_sidebar(app, *sidebar, "Display style"), "display style for grid detail");
    }
    popup = dynamic_cast<PopupList*>(app.TestTop());
    if (popup != nullptr) {
        expect(select_popup(app, *popup, 2), "grid detail selected");
    }
    expect(menu->TestDisplayStyle() == DisplayStyle::GridDetail, "grid detail applied");
    expect(menu->m_list->GetRow() == 3, "grid detail columns");
    expect(menu->m_list->GetPage() == 9, "grid detail page size");

    // The bool entry toggles in place and keeps settings open.
    sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    expect(sidebar != nullptr, "settings after grid detail");
    if (sidebar != nullptr) {
        expect(select_sidebar(app, *sidebar, "Download row"), "hide download row");
    }
    expect(!menu->TestShowDownloadRow(), "download row hidden");
    expect(menu->TestRowCount() == 2, "download row removed");

    sidebar = dynamic_cast<Sidebar*>(app.TestTop());
    expect(sidebar != nullptr, "settings after hiding download");
    if (sidebar != nullptr) {
        expect(select_sidebar(app, *sidebar, "Download row"), "show download row");
    }
    expect(menu->TestShowDownloadRow(), "download row shown again");
    expect(menu->TestRowCount() == 3, "download row back");
    press(app, Button::B);

    if (g_failures != 0) {
        std::cerr << g_failures << " UI test(s) failed\n";
        return 1;
    }
    std::cout << "launcher PC UI tests passed\n";
    return 0;
}
