#pragma once

#include "assets.hpp"
#include "games.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/scrolling_text.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace launcher::ui {

class MainMenu final : public menu::MenuBase {
public:
    MainMenu();
    ~MainMenu() override;

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

    auto GetShortTitle() const -> const char* override { return "Games"; }

private:
    void Rebuild();
    void Activate(s64 index);
    void StartInstall(const GameEntry& entry, const AssetsGame& catalog, bool force = false);
    void StartLauncherUpdate();
    void OpenOptions();
    auto RowCount() const -> s64;
    auto HasLauncherUpdate() const -> bool;

    Assets m_assets;
    Assets m_fetched;
    std::vector<GameEntry> m_rows;
    std::unique_ptr<List> m_list;
    ScrollingText m_scroll_name;
    s64 m_index{};
    int m_banner{};

    std::thread m_fetch;
    std::atomic_bool m_fetch_done{false};
    std::atomic_bool m_fetch_cancel{false};
    bool m_fetch_joined{};
    bool m_fetch_ok{};
    bool m_demo_shown{};
};

} // namespace launcher::ui
