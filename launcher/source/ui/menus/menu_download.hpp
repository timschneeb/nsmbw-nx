#pragma once

#include "assets.hpp"
#include "games.hpp"
#include "ui/banner.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/scrolling_text.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace launcher::ui {

class DownloadMenu final : public menu::MenuBase {
public:
    using BannerLookup = std::function<BannerImage(const GameEntry&)>;
    using UpdatedCallback = std::function<void()>;

    DownloadMenu(Assets* assets, BannerLookup banner_lookup, UpdatedCallback on_updated);

    void Update(Controller* controller, TouchInfo* touch) override;
    void Draw(NVGcontext* vg, Theme* theme) override;
    void OnFocusGained() override;

    auto GetShortTitle() const -> const char* override { return "Download"; }

#ifdef LAUNCHER_TEST
    auto TestRows() const -> const std::vector<GameEntry>& { return m_rows; }
    auto TestIndex() const -> s64 { return m_index; }
    auto TestLoaded() const -> bool { return m_loaded; }
#endif

private:
    void Rebuild();
    void Activate(s64 index);
    void StartFetch();
    void StartInstall(const GameEntry& entry);
    void InstallConfirmed(const GameEntry& entry, const AssetsGame& game);
    void ShowMessage(std::string message);
    void ShowMessageLater(std::string message);
    auto RowCount() const -> s64;

    Assets* m_assets;
    BannerLookup m_banner_lookup;
    UpdatedCallback m_on_updated;
    std::vector<GameEntry> m_rows;
    std::unique_ptr<List> m_list;
    ScrollingText m_scroll_name;
    ScrollingText m_scroll_author;
    s64 m_index{};
    std::string m_pending_message;
    bool m_loaded{};
    bool m_exit_after_error{};
};

} // namespace launcher::ui
