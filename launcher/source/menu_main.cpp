#include "menu_main.hpp"

#include "app.hpp"
#include "install.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/sidebar.hpp"
#include "ui/progress_box.hpp"

#include <filesystem>

#include <algorithm>
#include <utility>

extern "C" const unsigned char nsmbw_logo_rgba[];

namespace launcher::ui {
namespace {

constexpr int kLogoWidth = 820;
constexpr int kLogoHeight = 339;
constexpr float kRowHeight = 80.f;
constexpr s64 kRowsPerPage = 6;

} // namespace

MainMenu::MainMenu() : MenuBase{"Games", menu::MenuFlag_None} {
    SetTitleSubHeading("Select a game");

    SetActions(
        std::make_pair(Button::A, Action{"Launch", [this] {
            Activate(m_index);
        }}),
        std::make_pair(Button::B, Action{"Quit", [] {
            App::Exit();
        }}),
        std::make_pair(Button::X, Action{"Options", [this] {
            OpenOptions();
        }})
    );

    if (App::GetVg() != nullptr) {
        m_banner = nvgCreateImageRGBA(App::GetVg(), kLogoWidth, kLogoHeight, 0, nsmbw_logo_rgba);
    }
    log_write("[menu] banner image %d\n", m_banner);

    Rebuild();

    const Vec4 v{75.f, GetY() + 1.f + 42.f, 1220.f - 45.f * 2.f, kRowHeight};
    m_list = std::make_unique<List>(1, kRowsPerPage, GetPos(), v);

    m_fetch = std::thread([this] {
        Assets fetched;
        const Result rc = FetchAssets(fetched, &m_fetch_cancel);
        if (R_SUCCEEDED(rc)) {
            m_fetched = std::move(fetched);
            log_write("[menu] catalog loaded: %zu game(s)\n", m_fetched.games.size());
        } else {
            log_write("[menu] catalog unavailable 0x%08X\n", static_cast<unsigned>(rc));
        }
        m_fetch_ok = R_SUCCEEDED(rc);
        m_fetch_done = true;
    });
}

MainMenu::~MainMenu() {
    m_fetch_cancel = true;
    if (m_fetch.joinable()) {
        m_fetch.join();
    }
}

auto MainMenu::HasLauncherUpdate() const -> bool {
    return m_assets.valid && m_assets.LauncherUpdate(LauncherVersion());
}

auto MainMenu::RowCount() const -> s64 {
    return static_cast<s64>(m_rows.size()) + (HasLauncherUpdate() ? 1 : 0);
}

void MainMenu::Rebuild() {
    m_rows = ScanGames(m_assets);
    m_index = RowCount() > 0 ? std::clamp<s64>(m_index, 0, RowCount() - 1) : 0;
    log_write("[menu] %zu game(s)\n", m_rows.size());
}

void MainMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    Rebuild();

    if (!m_demo_shown && std::filesystem::exists(RootDirectory() + "/progress-demo")) {
        m_demo_shown = true;
        log_write("[menu] progress demo\n");
        App::Get().Push(std::make_unique<ProgressBox>(
            "Installing", "Test Mod",
            [](ProgressBox* pbox) {
                for (s64 i = 0; i <= 100; i++) {
                    if (pbox->ShouldExit()) {
                        return static_cast<Result>(MAKERESULT(Module_Libnx, LibnxError_BadInput));
                    }
                    if (i == 0) {
                        pbox->NewTransfer("Downloading game.nro");
                    } else if (i == 35) {
                        pbox->NewTransfer("Verifying game.nro");
                    } else if (i == 50) {
                        pbox->NewTransfer("Downloading resources.zip");
                    } else if (i == 85) {
                        pbox->NewTransfer("Extracting resources");
                    }
                    pbox->UpdateTransfer(i, 100);
                    svcSleepThread(100'000'000ULL);
                }
                return static_cast<Result>(0);
            },
            [](Result) {}));
    }
}

void MainMenu::Activate(s64 index) {
    if (index < 0) {
        return;
    }

    if (index >= static_cast<s64>(m_rows.size())) {
        StartLauncherUpdate();
        return;
    }

    const GameEntry& entry = m_rows[static_cast<size_t>(index)];
    if (entry.state == GameState::Launch) {
        if (entry.installed) {
            if (!IsValidNro(entry.nro_path)) {
                SetSubHeading("Not a valid NRO: " + entry.nro_path);
                return;
            }
            SetSubHeading("");
            App::Get().Launch(entry.nro_path);
        }
        return;
    }

    const auto catalog = m_assets.games.find(entry.id);
    if (catalog == m_assets.games.end()) {
        SetSubHeading("No download available for " + entry.name);
        return;
    }

    StartInstall(entry, catalog->second);
}

void MainMenu::StartInstall(const GameEntry& entry, const AssetsGame& catalog, bool force) {
    auto* menu = this;
    SetSubHeading("Installing " + entry.name);

    App::Get().Push(std::make_unique<ProgressBox>(
        "Installing", entry.name,
        [entry, catalog, force](ProgressBox* pbox) {
            return InstallGame(entry, catalog, *pbox, force);
        },
        [menu](Result rc) {
            menu->SetSubHeading(R_SUCCEEDED(rc) ? "" : "Install failed, see launcher.log");
            menu->Rebuild();
        }));
}

void MainMenu::StartLauncherUpdate() {
    if (!HasLauncherUpdate()) {
        return;
    }

    auto* menu = this;
    const Assets assets = m_assets;
    SetSubHeading("Updating launcher");

    App::Get().Push(std::make_unique<ProgressBox>(
        "Updating", "Launcher",
        [assets](ProgressBox* pbox) {
            return InstallLauncher(assets, *pbox);
        },
        [menu](Result rc) {
            menu->SetSubHeading(R_SUCCEEDED(rc) ? "Launcher updated, restart to apply" : "Update failed, see launcher.log");
            menu->Rebuild();
        }));
}

void MainMenu::OpenOptions() {
    if (m_index < 0 || m_index >= static_cast<s64>(m_rows.size())) {
        return;
    }

    auto* menu = this;
    const GameEntry entry = m_rows[static_cast<size_t>(m_index)];
    const auto catalog = m_assets.games.find(entry.id);
    const bool managed = catalog != m_assets.games.end();
    const AssetsGame game = managed ? catalog->second : AssetsGame{};

    if (!entry.installed) {
        return;
    }

    auto sidebar = std::make_unique<Sidebar>("Game Options", entry.name, Sidebar::Side::RIGHT);

    if (managed) {
        sidebar->Add<SidebarEntryCallback>("Update", [menu, entry, game] {
            menu->StartInstall(entry, game, true);
        }, true);
    } else {
        sidebar->Add<SidebarEntryCallback>("Update", [menu] {
            menu->SetSubHeading("No download available for this game");
        }, true);
    }

    sidebar->Add<SidebarEntryCallback>("Remove", [menu, entry] {
        App::Get().Push(std::make_unique<OptionBox>(
            entry.name + "\nRemove deletes the NRO, data and saves.",
            "No", "Remove", 0, [menu, entry](std::optional<s64> index) {
                if (index && *index) {
                    RemoveGame(entry.dir);
                    menu->Rebuild();
                }
            }));
    }, true);

    App::Get().Push(std::move(sidebar));
}

void MainMenu::Update(Controller* controller, TouchInfo* touch) {
    if (m_fetch_done && !m_fetch_joined) {
        if (m_fetch.joinable()) {
            m_fetch.join();
        }
        m_fetch_joined = true;
        if (m_fetch_ok) {
            m_assets = std::move(m_fetched);
            Rebuild();
        }
    }

    m_list->OnUpdate(controller, touch, m_index, RowCount(), [this](bool touch, s64 i) {
        if (touch && m_index == i) {
            Activate(i);
            return;
        }

        m_index = i;
        m_scroll_name.Reset();
    });

    MenuBase::Update(controller, touch);
}

void MainMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    if (RowCount() == 0) {
        gfx::drawText(vg, GetX() + GetW() / 2.f, GetY() + GetH() / 2.f, 36.f,
                      theme->GetColour(ThemeEntryID_TEXT_INFO), "No games installed",
                      NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }

    m_list->Draw(vg, theme, RowCount(), [this](NVGcontext* vg, Theme* theme, const Vec4& v, s64 i) {
        const auto& [x, y, w, h] = v;

        std::string name;
        std::string meta;
        auto meta_id = ThemeEntryID_TEXT_INFO;

        if (i < static_cast<s64>(m_rows.size())) {
            const GameEntry& entry = m_rows[static_cast<size_t>(i)];
            name = entry.name;
            switch (entry.state) {
                case GameState::Install:
                    meta = "Install";
                    meta_id = ThemeEntryID_TEXT_SELECTED;
                    break;
                case GameState::Update:
                    meta = "Update";
                    meta_id = ThemeEntryID_TEXT_SELECTED;
                    break;
                case GameState::Launch:
                    meta = entry.version;
                    break;
            }
        } else {
            name = "Launcher update";
            meta = "v" + m_assets.launcher_version;
            meta_id = ThemeEntryID_TEXT_SELECTED;
        }

        auto text_id = ThemeEntryID_TEXT;
        const auto selected = m_index == i;
        if (selected) {
            text_id = ThemeEntryID_TEXT_SELECTED;
            gfx::drawRectOutline(vg, theme, 4.f, v);
        } else if (i != RowCount() - 1) {
            gfx::drawRect(vg, Vec4{x, y + h, w, 1.f}, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        const float banner_h = h - 10.f;
        const float banner_w = banner_h * static_cast<float>(kLogoWidth) / static_cast<float>(kLogoHeight);
        gfx::drawImage(vg, x + 15.f, y + 5.f, banner_w, banner_h, m_banner, 2.f);

        const float text_x = x + 15.f + banner_w + 15.f;
        m_scroll_name.Draw(vg, selected, text_x, y + (h / 2.f), w - (text_x - x) - 220.f, 20.f,
                           NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), name);

        gfx::drawText(vg, x + w - 15.f, y + (h / 2.f), 16.f, theme->GetColour(meta_id),
                      meta.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    });
}

} // namespace launcher::ui
