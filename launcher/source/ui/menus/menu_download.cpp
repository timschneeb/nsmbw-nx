#include "ui/menus/menu_download.hpp"

#include "app.hpp"
#include "basedata.hpp"
#include "games.hpp"
#include "install.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace launcher::ui {
namespace {

constexpr float kRowHeight = 80.f;
constexpr s64 kRowsPerPage = 6;
constexpr float kBannerBoxWidth = 170.f;

} // namespace

DownloadMenu::DownloadMenu(Assets* assets, BannerLookup banner_lookup, UpdatedCallback on_updated)
    : MenuBase{"Download", menu::MenuFlag_None}
    , m_assets{assets}
    , m_banner_lookup{std::move(banner_lookup)}
    , m_on_updated{std::move(on_updated)} {
    SetTitleSubHeading("Not installed");

    SetActions(
        std::make_pair(Button::A, Action{"Install", [this] {
            Activate(m_index);
        }}),
        std::make_pair(Button::B, Action{"Back", [this] {
            SetPop();
        }})
    );

    Rebuild();

    const Vec4 v{75.f, GetY() + 1.f + 42.f, 1220.f - 45.f * 2.f, kRowHeight};
    m_list = std::make_unique<List>(1, kRowsPerPage, GetPos(), v);
}

auto DownloadMenu::RowCount() const -> s64 {
    return static_cast<s64>(m_rows.size());
}

void DownloadMenu::Rebuild() {
    auto all = ScanGames(*m_assets);
    m_rows.clear();
    for (auto& entry : all) {
        if (entry.state == GameState::Install) {
            m_rows.push_back(std::move(entry));
        }
    }
    std::sort(m_rows.begin(), m_rows.end(), [](const GameEntry& a, const GameEntry& b) {
        return a.name < b.name;
    });
    m_index = RowCount() > 0 ? std::clamp<s64>(m_index, 0, RowCount() - 1) : 0;
    m_scroll_name.Reset();
    m_scroll_author.Reset();
}

void DownloadMenu::OnFocusGained() {
    MenuBase::OnFocusGained();

    if (!m_pending_message.empty()) {
        auto message = std::move(m_pending_message);
        m_pending_message.clear();
        Rebuild();
        App::Get().Push(std::make_unique<OptionBox>(message, "Close"));
        return;
    }

    if (m_exit_after_error) {
        m_exit_after_error = false;
        SetPop();
        return;
    }

    Rebuild();

    if (m_loaded) {
        return;
    }
    m_loaded = true;
    StartFetch();
}

void DownloadMenu::StartFetch() {
    auto fetched = std::make_shared<Assets>();
    auto canceled = std::make_shared<bool>(false);
    auto* menu = this;

    App::Get().Push(std::make_unique<ProgressBox>(
        "Loading", "Catalogue",
        [fetched, canceled](ProgressBox* pbox) {
            return FetchRemoteAssets(*fetched, [pbox, canceled] {
                const bool exit = pbox->ShouldExit();
                if (exit) {
                    *canceled = true;
                }
                return exit;
            });
        },
        [menu, fetched, canceled](Result rc) {
            if (*canceled) {
                menu->SetPop();
                return;
            }

            if (R_SUCCEEDED(rc) && !fetched->games.empty()) {
                *menu->m_assets = std::move(*fetched);
                if (menu->m_on_updated) {
                    menu->m_on_updated();
                }
                menu->Rebuild();
                return;
            }

            menu->ShowMessageLater(R_SUCCEEDED(rc) ? "The catalogue has no available games."
                                                 : "Could not load the catalogue. Check the network connection.");
            menu->m_exit_after_error = true;
            menu->Rebuild();
        }));
}

void DownloadMenu::Activate(s64 index) {
    if (index < 0 || index >= RowCount()) {
        return;
    }
    StartInstall(m_rows[static_cast<size_t>(index)]);
}

void DownloadMenu::StartInstall(const GameEntry& entry) {
    const auto catalog = m_assets->games.find(entry.id);
    if (catalog == m_assets->games.end()) {
        ShowMessage("No download available for " + entry.name + ".");
        return;
    }

    const AssetsGame game = catalog->second;
    std::string message = InstallPromptMessage(entry, game, "Install");
    if (!BaseDataReady()) {
        message += "\nBase game data is missing; extract it before playing.";
    }

    auto* menu = this;
    App::Get().Push(std::make_unique<OptionBox>(
        message, "Cancel", "Install", 0, [menu, entry, game](std::optional<s64> choice) {
            if (choice && *choice) {
                menu->InstallConfirmed(entry, game);
            }
        }));
}

void DownloadMenu::InstallConfirmed(const GameEntry& entry, const AssetsGame& game) {
    auto* menu = this;
    auto error = std::make_shared<std::string>();

    App::Get().Push(std::make_unique<ProgressBox>(
        "Installing", entry.name,
        [entry, game, error](ProgressBox* pbox) {
            return InstallGame(entry, game, *pbox, *error, false);
        },
        [menu, entry, error](Result rc) {
            if (R_SUCCEEDED(rc)) {
                TouchUsage(entry.id);
                menu->ShowMessageLater("Installed " + entry.name + ".");
            } else if (!error->empty()) {
                if (*error != "Install cancelled.") {
                    menu->ShowMessageLater(*error);
                }
            } else {
                menu->ShowMessageLater("Install failed, see launcher.log.");
            }
            menu->Rebuild();
        }));
}

void DownloadMenu::ShowMessage(std::string message) {
    App::Get().Push(std::make_unique<OptionBox>(std::move(message), "Close"));
}

void DownloadMenu::ShowMessageLater(std::string message) {
    m_pending_message = std::move(message);
}

void DownloadMenu::Update(Controller* controller, TouchInfo* touch) {
    m_list->OnUpdate(controller, touch, m_index, RowCount(), [this](bool touch, s64 i) {
        if (touch && m_index == i) {
            Activate(i);
            return;
        }

        m_index = i;
        m_scroll_name.Reset();
        m_scroll_author.Reset();
    });

    MenuBase::Update(controller, touch);
}

void DownloadMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (RowCount() == 0) {
        const char* message = m_assets->games.empty() ? "No catalogue available" : "All games are installed";
        gfx::drawText(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f,
                      theme->GetColour(ThemeEntryID_TEXT_INFO), message,
                      NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }

    m_list->Draw(vg, theme, RowCount(), [this](NVGcontext* vg, Theme* theme, const Vec4& v, s64 i) {
        const auto& [x, y, w, h] = v;
        const GameEntry& entry = m_rows[static_cast<size_t>(i)];

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = m_index == i;
        if (selected) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != RowCount() - 1) {
            gfx::drawRect(vg, Vec4{x, y + h, w, 1.f}, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        const auto banner = m_banner_lookup(entry);
        const float banner_h = h - 10.f;
        float draw_w = 0.f;
        float draw_h = 0.f;
        if (banner.width > 0 && banner.height > 0) {
            const float scale = std::min(kBannerBoxWidth / static_cast<float>(banner.width),
                                         banner_h / static_cast<float>(banner.height));
            draw_w = static_cast<float>(banner.width) * scale;
            draw_h = static_cast<float>(banner.height) * scale;
        }
        if (draw_w <= 0.f || draw_h <= 0.f) {
            draw_w = banner_h;
            draw_h = banner_h;
        }
        const float banner_x = x + 15.f + (kBannerBoxWidth - draw_w) / 2.f;
        const float banner_y = y + 5.f + (banner_h - draw_h) / 2.f;
        gfx::drawImage(vg, banner_x, banner_y, draw_w, draw_h, banner.image, 2.f);

        const float text_x = x + 15.f + kBannerBoxWidth + 15.f;
        const float text_w = w - (text_x - x) - 220.f;
        if (entry.author.empty()) {
            m_scroll_name.Draw(vg, selected, text_x, y + (h / 2.f), text_w, 20.f,
                               NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), entry.name);
        } else {
            const auto author_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO;
            m_scroll_name.Draw(vg, selected, text_x, y + (h / 2.f) - 11.f, text_w, 20.f,
                               NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), entry.name);
            m_scroll_author.Draw(vg, selected, text_x, y + (h / 2.f) + 11.f, text_w, 16.f,
                                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                                 theme->GetColour(author_id), entry.author);
        }

        std::string meta = "Install";
        if (const auto catalog = m_assets->games.find(entry.id); catalog != m_assets->games.end()) {
            const s64 size = InstallDownloadSize(catalog->second);
            if (size > 0) {
                meta = FormatSize(size);
            }
        }
        gfx::drawText(vg, x + w - 15.f, y + (h / 2.f), 16.f,
                      theme->GetColour(ThemeEntryID_TEXT_SELECTED), meta.c_str(),
                      NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    });
}

} // namespace launcher::ui
