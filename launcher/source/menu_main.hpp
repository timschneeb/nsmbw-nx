#pragma once

#include "assets.hpp"
#include "basedata.hpp"
#include "games.hpp"
#include "settings.hpp"
#include "ui/banner.hpp"
#include "ui/list.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/scrolling_text.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>
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
    struct RowVisual {
        std::string name;
        std::string subtitle;
        std::string meta;
        ThemeEntryID meta_id{ThemeEntryID_TEXT_INFO};
        bool update_pill{};
        bool game{};
        bool launcher{};
        bool download{};
    };

    struct IconImage {
        int image{};
        int width{};
        int height{};
        long long file_size{};
        long long mtime{};
    };

    void RefreshBanners();
    void RefreshIcons();
    void Rebuild();
    void ApplyDisplayStyle();
    void SetDisplayStyle(DisplayStyle style);
    void SetShowDownloadRow(bool show);
    auto DescribeRow(s64 index) const -> RowVisual;
    auto IconFor(const GameEntry& entry) const -> const IconImage*;
    void DrawDetailRow(NVGcontext* vg, Theme* theme, const Vec4& v, s64 index);
    void DrawBannerGridTile(NVGcontext* vg, Theme* theme, const Vec4& v, s64 index);
    void DrawGridDetailTile(NVGcontext* vg, Theme* theme, const Vec4& v, s64 index);
    void Activate(s64 index);
    void StartInstall(const GameEntry& entry, const AssetsGame& catalog, bool force = false);
    void StartRemove(const GameEntry& entry, bool keep_data);
    void StartVerify(const GameEntry& entry);
    void CheckBaseDataOnBoot();
    void PromptBaseData();
    void StartBaseExtraction(const BaseImage& image, bool replace_existing);
    void ConfirmInstall(const GameEntry& entry, const AssetsGame& catalog, bool force,
                        const std::string& action, const std::function<void()>& on_confirm = {});
    void CheckGameUpdate(const GameEntry& entry);
    void UpdateSelected();
    void CheckLauncherUpdate();
    void FetchCatalogue(const std::string& title,
                        const std::function<void(Result, bool)>& done);
    void ConfirmUpdateAll();
    void ShowUpdateAllConfirmation();
    void StartUpdateAll();
    void StartLauncherUpdate();
    void OpenOptions();
    void OpenLauncherOptions();
    void OpenDownloads();
    void UpdateFooter();
    void LaunchNow(const GameEntry& entry);
    void LaunchWithDiscCheck(const GameEntry& entry);
    void ShowError(std::string message);
    void ShowMessage(std::string message);
    void ShowMessageLater(std::string message);
    void ShowDialogLater(std::function<void()> dialog);
    auto RowCount() const -> s64;
    auto HasLauncherUpdate() const -> bool;
    auto BannerFor(const GameEntry& entry) const -> BannerImage;

public:
#ifdef LAUNCHER_TEST
    auto TestRows() const -> const std::vector<GameEntry>& { return m_rows; }
    auto TestIndex() const -> s64 { return m_index; }
    auto TestRowCount() const -> s64 { return RowCount(); }
    auto TestDisplayStyle() const -> DisplayStyle { return m_settings.display_style; }
    auto TestShowDownloadRow() const -> bool { return m_settings.show_download_row; }
#endif

    Assets m_assets;
    LauncherSettings m_settings;
    std::vector<GameEntry> m_rows;
    std::unique_ptr<List> m_list;
    ScrollingText m_scroll_name;
    ScrollingText m_scroll_author;
    s64 m_index{};
    s64 m_download_count{};
    bool m_boot_checked{};
    bool m_base_ready{};
    int m_fallback_banner{};
    std::map<std::string, BannerImage> m_banners;
    std::map<std::string, IconImage> m_icons;
    std::vector<int> m_banner_discard_pending;
    std::vector<int> m_banner_discard_ready;
    std::string m_pending_message;
    std::function<void()> m_pending_dialog;
};

} // namespace launcher::ui
