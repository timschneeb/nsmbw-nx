#include "menu_main.hpp"

#include "app.hpp"
#include "basedata.hpp"
#include "install.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "ui/menus/menu_download.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"
#include "ui/progress_box.hpp"
#include "ui/sidebar.hpp"

#include <algorithm>
#include <filesystem>
#include <utility>

extern "C" const unsigned char nsmbw_logo_rgba[];

namespace launcher::ui {
namespace {

constexpr int kLogoWidth = 820;
constexpr int kLogoHeight = 339;
constexpr float kRowHeight = 102.f;
constexpr s64 kRowsPerPage = 5;
constexpr float kBannerBoxWidth = 240.f;

auto BannerPath(const std::string& id, const Banner& banner) -> std::string {
    std::error_code ec;
    if (!banner.path.empty() && std::filesystem::is_regular_file(banner.path, ec)) {
        return banner.path;
    }
    return (std::filesystem::path(BannersDirectory()) / (id + ".png")).string();
}

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
        std::make_pair(Button::X, Action{"Game options", [this] {
            OpenOptions();
        }}),
        std::make_pair(Button::SELECT, Action{"Settings", [this] {
            OpenLauncherOptions();
        }})
    );

    if (App::GetVg() != nullptr) {
        m_fallback_banner = nvgCreateImageRGBA(App::GetVg(), kLogoWidth, kLogoHeight, 0, nsmbw_logo_rgba);
    }
    log_write("[menu] banner image %d\n", m_fallback_banner);

    LoadLocalAssets(m_assets);
    Rebuild();
    RefreshBanners();

    const Vec4 v{75.f, GetY() + 1.f + 42.f, 1220.f - 45.f * 2.f, kRowHeight};
    m_list = std::make_unique<List>(1, kRowsPerPage, GetPos(), v);
}

MainMenu::~MainMenu() {
    if (App::GetVg() != nullptr) {
        for (const auto& [id, banner] : m_banners) {
            (void)id;
            if (banner.image > 0) {
                nvgDeleteImage(App::GetVg(), banner.image);
            }
        }
        for (int image : m_banner_discard_pending) {
            if (image > 0) {
                nvgDeleteImage(App::GetVg(), image);
            }
        }
        for (int image : m_banner_discard_ready) {
            if (image > 0) {
                nvgDeleteImage(App::GetVg(), image);
            }
        }
        if (m_fallback_banner > 0) {
            nvgDeleteImage(App::GetVg(), m_fallback_banner);
        }
    }
}

auto MainMenu::HasLauncherUpdate() const -> bool {
    return m_assets.valid && m_assets.LauncherUpdate(LauncherVersion());
}

auto MainMenu::ShowLauncherRow() const -> bool {
#ifdef __SWITCH__
    // Temporary layout preview: keep the launcher update row visible.
    return true;
#else
    return HasLauncherUpdate();
#endif
}

auto MainMenu::RowCount() const -> s64 {
    // Installed games, the optional launcher update, and the always present download entry.
    return static_cast<s64>(m_rows.size()) + (ShowLauncherRow() ? 1 : 0) + 1;
}

void MainMenu::Rebuild() {
    m_base_ready = BaseDataReady();
    auto all = ScanGames(m_assets);
    m_rows.clear();
    m_download_count = 0;
    for (auto& entry : all) {
        if (entry.state == GameState::Install) {
            m_download_count++;
            continue;
        }
        entry.base_ready = m_base_ready || BaseDataReadyFor(entry);
        m_rows.push_back(std::move(entry));
    }
    std::sort(m_rows.begin(), m_rows.end(), [](const GameEntry& a, const GameEntry& b) {
        if (a.last_used != b.last_used) {
            return a.last_used > b.last_used;
        }
        return a.name < b.name;
    });
    m_index = std::clamp<s64>(m_index, 0, RowCount() - 1);
    m_scroll_name.Reset();
    m_scroll_author.Reset();
    UpdateFooter();
    log_write("[menu] %zu installed game(s), %lld available\n", m_rows.size(),
              static_cast<long long>(m_download_count));
}

void MainMenu::RefreshBanners() {
    if (App::GetVg() == nullptr) {
        return;
    }

    std::map<std::string, BannerImage> next;
    for (const auto& [id, catalog] : m_assets.games) {
        if (!catalog.banner.Valid()) {
            continue;
        }
        const auto path = BannerPath(id, catalog.banner);
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec) {
            continue;
        }

        const auto file_size = static_cast<long long>(std::filesystem::file_size(path, ec));
        if (ec) {
            continue;
        }
        std::error_code time_ec;
        const auto stamp = std::filesystem::last_write_time(path, time_ec);
        const auto mtime = time_ec ? 0LL : static_cast<long long>(stamp.time_since_epoch().count());

        const auto current = m_banners.find(id);
        if (current != m_banners.end() && current->second.file_size == file_size &&
            current->second.mtime == mtime) {
            next[id] = current->second;
            continue;
        }

        const int image = nvgCreateImage(App::GetVg(), path.c_str(), 0);
        if (image <= 0) {
            log_write("[menu] cannot decode banner %s\n", path.c_str());
            continue;
        }
        int width = 0;
        int height = 0;
        nvgImageSize(App::GetVg(), image, &width, &height);
        if (width <= 0 || height <= 0) {
            nvgDeleteImage(App::GetVg(), image);
            continue;
        }
        if (current != m_banners.end() && current->second.image > 0) {
            m_banner_discard_pending.push_back(current->second.image);
        }
        next[id] = BannerImage{image, width, height, file_size, mtime};
    }

    for (const auto& [id, banner] : m_banners) {
        if (next.contains(id)) {
            continue;
        }
        if (banner.image > 0) {
            m_banner_discard_pending.push_back(banner.image);
        }
    }

    m_banners = std::move(next);
}

auto MainMenu::BannerFor(const GameEntry& entry) const -> BannerImage {
    const auto it = m_banners.find(entry.id);
    if (it != m_banners.end()) {
        return it->second;
    }
    return BannerImage{m_fallback_banner, kLogoWidth, kLogoHeight};
}

void MainMenu::OnFocusGained() {
    MenuBase::OnFocusGained();
    Rebuild();

    if (!m_boot_checked) {
        m_boot_checked = true;
        CheckBaseDataOnBoot();
        return;
    }

    // Pushing from the progress worker's done callback would modify the widget
    // stack while App::Run is popping it. Show the message once focus is back.
    if (!m_pending_message.empty()) {
        auto message = std::move(m_pending_message);
        m_pending_message.clear();
        App::Get().Push(std::make_unique<OptionBox>(message, "Close"));
        return;
    }

    if (m_pending_dialog) {
        auto dialog = std::move(m_pending_dialog);
        m_pending_dialog = {};
        dialog();
    }
}

void MainMenu::ShowError(std::string message) {
    App::Get().Push(std::make_unique<OptionBox>(std::move(message), "Close"));
}

void MainMenu::ShowMessage(std::string message) {
    App::Get().Push(std::make_unique<OptionBox>(std::move(message), "Close"));
}

void MainMenu::ShowMessageLater(std::string message) {
    m_pending_message = std::move(message);
}

void MainMenu::ShowDialogLater(std::function<void()> dialog) {
    m_pending_dialog = std::move(dialog);
}

void MainMenu::Activate(s64 index) {
    if (index < 0) {
        return;
    }

    const auto games = static_cast<s64>(m_rows.size());
    if (index >= games) {
        if (ShowLauncherRow() && index == games) {
            if (HasLauncherUpdate()) {
                StartLauncherUpdate();
            } else {
                CheckLauncherUpdate();
            }
        } else {
            OpenDownloads();
        }
        return;
    }

    const GameEntry& entry = m_rows[static_cast<size_t>(index)];
    if (entry.installed) {
        if (!IsValidNro(entry.nro_path)) {
            ShowError("Not a valid NRO\n" + entry.nro_path);
            return;
        }
        LaunchWithDiscCheck(entry);
        return;
    }

    const auto catalog = m_assets.games.find(entry.id);
    if (catalog == m_assets.games.end()) {
        ShowError("No download is available for " + entry.name + ".");
        return;
    }

    StartInstall(entry, catalog->second);
}

void MainMenu::LaunchWithDiscCheck(const GameEntry& entry) {
    if (entry.base_ready || BaseDataReadyFor(entry)) {
        LaunchNow(entry);
        return;
    }

    auto* menu = this;
    const GameEntry copy = entry;
    const std::string message =
        "Base game data is missing\nPlace a USA Revision 1 (SMNE01) disc image in this folder:\n" +
        RootDirectory() +
        "\nRestart the launcher, or set [paths] dvd_root to an extracted game folder.";
    App::Get().Push(std::make_unique<OptionBox>(
        message, "Cancel", "Launch anyway", 0, [menu, copy](std::optional<s64> choice) {
            if (choice && *choice) {
                menu->LaunchNow(copy);
            }
        }));
}

void MainMenu::LaunchNow(const GameEntry& entry) {
    TouchUsage(entry.id);
    SetSubHeading("");
    App::Get().Launch(entry.nro_path);
}

void MainMenu::StartInstall(const GameEntry& entry, const AssetsGame& catalog, bool force) {
    auto* menu = this;
    auto error = std::make_shared<std::string>();

    App::Get().Push(std::make_unique<ProgressBox>(
        "Installing", entry.name,
        [entry, catalog, force, error](ProgressBox* pbox) {
            return InstallGame(entry, catalog, *pbox, *error, force);
        },
        [menu, entry, error](Result rc) {
            if (R_SUCCEEDED(rc)) {
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

void MainMenu::StartRemove(const GameEntry& entry, bool keep_data) {
    auto* menu = this;
    auto box = std::make_unique<ProgressBox>(
        "Deleting", entry.name,
        [entry, keep_data](ProgressBox* pbox) {
            pbox->SetTransfer(keep_data ? "Deleting " + entry.name + " files"
                                        : "Deleting " + entry.name + " and save data");
            return RemoveGame(entry.dir, entry.id, keep_data);
        },
        [menu, entry, keep_data](Result rc) {
            if (R_SUCCEEDED(rc)) {
                menu->ShowMessageLater(keep_data ? "Deleted " + entry.name + "."
                                                 : "Deleted " + entry.name + " and save data.");
            } else {
                menu->ShowMessageLater("Could not delete " + entry.name + ". See launcher.log.");
            }
            menu->Rebuild();
        });
    // Deletion cannot be interrupted safely, so do not offer a cancel that would
    // only make the dialog wait for the worker.
    box->RemoveAction(Button::B);
    App::Get().Push(std::move(box));
}

void MainMenu::StartVerify(const GameEntry& entry) {
    auto* menu = this;
    auto error = std::make_shared<std::string>();

    App::Get().Push(std::make_unique<ProgressBox>(
        "Verifying", entry.name,
        [entry, error](ProgressBox* pbox) {
            return VerifyGame(entry, *pbox, *error);
        },
        [menu, entry, error](Result rc) {
            if (R_SUCCEEDED(rc)) {
                menu->ShowMessageLater("Files verified for " + entry.name + ".");
            } else if (!error->empty()) {
                if (*error != "Install cancelled.") {
                    menu->ShowMessageLater(*error);
                }
            } else {
                menu->ShowMessageLater("Verification failed, see launcher.log.");
            }
            menu->Rebuild();
        }));
}

void MainMenu::ConfirmInstall(const GameEntry& entry, const AssetsGame& catalog, bool force,
                              const std::string& action, const std::function<void()>& on_confirm) {
    auto* menu = this;
    std::string message = InstallPromptMessage(entry, catalog, action, force);
    if (!entry.base_ready) {
        message += "\nBase game data is missing; a USA Revision 1 (SMNE01) disc image is required.";
    }
    App::Get().Push(std::make_unique<OptionBox>(
        message, "Cancel", action, 0,
        [menu, entry, catalog, force, on_confirm](std::optional<s64> choice) {
            if (choice && *choice) {
                if (on_confirm) {
                    on_confirm();
                }
                menu->StartInstall(entry, catalog, force);
            }
        }));
}

void MainMenu::FetchCatalogue(const std::string& title,
                              const std::function<void(Result, bool)>& done) {
    auto fetched = std::make_shared<Assets>();
    auto canceled = std::make_shared<bool>(false);
    auto* menu = this;

    App::Get().Push(std::make_unique<ProgressBox>(
        "Checking", title,
        [fetched, canceled](ProgressBox* pbox) {
            return FetchRemoteAssets(*fetched, [pbox, canceled] {
                const bool exit = pbox->ShouldExit();
                if (exit) {
                    *canceled = true;
                }
                return exit;
            });
        },
        [menu, fetched, canceled, done](Result rc) {
            if (R_SUCCEEDED(rc)) {
                menu->m_assets = std::move(*fetched);
                menu->RefreshBanners();
                menu->Rebuild();
            }
            done(rc, *canceled);
        }));
}

void MainMenu::ConfirmUpdateAll() {
    FetchCatalogue("Update all", [this](Result rc, bool canceled) {
        if (canceled) {
            return;
        }
        if (R_FAILED(rc)) {
            ShowMessageLater("Could not check for updates. Check the network connection.");
            return;
        }
        ShowDialogLater([this] { ShowUpdateAllConfirmation(); });
    });
}

void MainMenu::ShowUpdateAllConfirmation() {
    std::vector<std::pair<GameEntry, AssetsGame>> updates;
    s64 download = 0;
    s64 required = 0;
    for (const auto& entry : m_rows) {
        if (entry.state != GameState::Update) {
            continue;
        }
        const auto catalog = m_assets.games.find(entry.id);
        if (catalog == m_assets.games.end()) {
            continue;
        }
        download += InstallTransferSize(entry, catalog->second);
        required += InstallRequiredSpace(entry, catalog->second);
        updates.emplace_back(entry, catalog->second);
    }

    if (updates.empty()) {
        ShowMessage("Everything is up to date.");
        return;
    }

    s64 free = 0;
    s64 total = 0;
    const bool have_free = R_SUCCEEDED(App::GetSdSize(&free, &total));
    std::string message = "Update " + std::to_string(updates.size()) +
                          (updates.size() == 1 ? " game?" : " games?") + "\n";
    if (download > 0) {
        message += "Download " + FormatSize(download) + ". ";
    }
    message += "Needs " + FormatSize(required) + " free";
    if (have_free) {
        message += ", " + FormatSize(free) + " available";
    }
    message += ".";

    auto* menu = this;
    App::Get().Push(std::make_unique<OptionBox>(
        message, "Cancel", "Update all", 1,
        [menu](std::optional<s64> choice) {
            if (choice && *choice) {
                menu->StartUpdateAll();
            }
        }));
}

void MainMenu::CheckGameUpdate(const GameEntry& entry) {
    const std::string id = entry.id;
    const std::string name = entry.name;
    FetchCatalogue(name, [this, id, name](Result rc, bool canceled) {
        if (canceled) {
            return;
        }
        if (R_FAILED(rc)) {
            ShowMessageLater("Could not check for updates. Check the network connection.");
            return;
        }

        const auto row = std::find_if(m_rows.begin(), m_rows.end(),
                                      [&id](const GameEntry& entry) { return entry.id == id; });
        const auto catalog = m_assets.games.find(id);
        if (row == m_rows.end() || catalog == m_assets.games.end()) {
            ShowMessageLater("This game is no longer in the catalogue.");
            return;
        }

        if (row->state == GameState::Update) {
            const GameEntry updated = *row;
            const AssetsGame game = catalog->second;
            ShowDialogLater([this, updated, game] {
                std::string message = InstallPromptMessage(updated, game, "Update");
                App::Get().Push(std::make_unique<OptionBox>(
                    message, "Cancel", "Update", 1,
                    [this, updated, game](std::optional<s64> choice) {
                        if (choice && *choice) {
                            StartInstall(updated, game, false);
                        }
                    }));
            });
        } else {
            ShowMessageLater(name + " is up to date.");
        }
    });
}

void MainMenu::CheckLauncherUpdate() {
    FetchCatalogue("Launcher", [this](Result rc, bool canceled) {
        if (canceled) {
            return;
        }
        if (R_FAILED(rc)) {
            ShowMessageLater("Could not check for launcher updates. Check the network connection.");
            return;
        }
        if (!HasLauncherUpdate()) {
            ShowMessageLater("Launcher is up to date.");
            return;
        }

        const std::string version = m_assets.launcher_version;
        ShowDialogLater([this, version] {
            App::Get().Push(std::make_unique<OptionBox>(
                "Launcher update available\nVersion " + version + " is ready to install.",
                "Cancel", "Update", 1, [this](std::optional<s64> choice) {
                    if (choice && *choice) {
                        StartLauncherUpdate();
                    }
                }));
        });
    });
}

void MainMenu::CheckBaseDataOnBoot() {
    CleanupBaseImport();

    // A configured dvd_root keeps the runtime working even when discs/SMNE01 is absent.
    if (std::any_of(m_rows.begin(), m_rows.end(), [](const GameEntry& entry) {
            return entry.base_ready;
        })) {
        return;
    }

    PromptBaseData();
}

void MainMenu::PromptBaseData() {
    std::string problem;
    const BaseDataState state = CheckBaseData(problem);
    log_write("[basedata] state=%d target=%s problem=%s\n", static_cast<int>(state),
              BaseDiscDirectory().c_str(), problem.c_str());
    if (state == BaseDataState::Ready) {
        return;
    }

    std::string image_problem;
    const auto image = FindBaseImage(image_problem);
    if (image) {
        log_write("[basedata] image=%s id=%s rev=%d extracted=%lld\n", image->path.c_str(), image->id.c_str(),
                  image->revision, static_cast<long long>(image->extracted_size));
    } else {
        log_write("[basedata] no usable image: %s\n", image_problem.c_str());
    }

    if (state == BaseDataState::Incomplete) {
        if (!image) {
            std::string message = "Base game data can't be used\n" + problem;
            if (!image_problem.empty()) {
                message += "\n" + image_problem;
            }
            App::Get().Push(std::make_unique<OptionBox>(message, "Close"));
            return;
        }

        auto* menu = this;
        App::Get().Push(std::make_unique<OptionBox>(
            "Replace the base game data?\n" + problem + " Re-extract from " +
                std::filesystem::path(image->path).filename().string() + "?",
            "Cancel", "Re-extract", 0, [menu, image](std::optional<s64> choice) {
                if (choice && *choice) {
                    menu->StartBaseExtraction(*image, true);
                }
            }));
        return;
    }

    if (image) {
        StartBaseExtraction(*image, false);
        return;
    }

    std::string message =
        "Base game data is missing\nPlace a USA Revision 1 (SMNE01) New Super Mario Bros. Wii disc image "
        "(.iso, .wbfs, .ciso, .wia or .rvz) in this folder:\n" +
        RootDirectory() +
        "\nRestart the launcher. The exact size and free space are checked when an image is found.";
    if (!problem.empty()) {
        message += "\n" + problem;
    }
    if (!image_problem.empty()) {
        message += "\n" + image_problem;
    }
    App::Get().Push(std::make_unique<OptionBox>(message, "Close"));
}

void MainMenu::StartBaseExtraction(const BaseImage& image, bool replace_existing) {
    auto* menu = this;
    auto error = std::make_shared<std::string>();

    App::Get().Push(std::make_unique<ProgressBox>(
        "Extracting", "Base game data",
        [image, replace_existing, error](ProgressBox* pbox) {
            return InstallBaseImage(image, *pbox, *error, replace_existing);
        },
        [menu, error](Result rc) {
            if (R_SUCCEEDED(rc)) {
                menu->ShowMessageLater("Base game data ready.");
            } else if (error->empty()) {
                menu->ShowMessageLater("Base game data extraction failed. See launcher.log.");
            } else if (*error != "Base data extraction cancelled.") {
                menu->ShowMessageLater(*error);
            }
            menu->Rebuild();
        }));
}

void MainMenu::StartUpdateAll() {
    std::vector<std::pair<GameEntry, AssetsGame>> updates;
    for (const auto& entry : m_rows) {
        if (entry.state != GameState::Update) {
            continue;
        }
        const auto catalog = m_assets.games.find(entry.id);
        if (catalog != m_assets.games.end()) {
            updates.emplace_back(entry, catalog->second);
        }
    }

    if (updates.empty()) {
        ShowMessage("Everything is up to date.");
        return;
    }

    auto* menu = this;
    auto error = std::make_shared<std::string>();

    App::Get().Push(std::make_unique<ProgressBox>(
        "Updating", "All games",
        [updates, error](ProgressBox* pbox) {
            for (const auto& [entry, catalog] : updates) {
                pbox->SetTitle(entry.name);
                const Result rc = InstallGame(entry, catalog, *pbox, *error, false);
                if (R_FAILED(rc)) {
                    return rc;
                }
            }
            return static_cast<Result>(0);
        },
        [menu, error](Result rc) {
            if (R_SUCCEEDED(rc)) {
                menu->ShowMessageLater("All games updated.");
            } else if (!error->empty()) {
                if (*error != "Install cancelled.") {
                    menu->ShowMessageLater(*error);
                }
            } else {
                menu->ShowMessageLater("Update failed, see launcher.log.");
            }
            menu->Rebuild();
        }));
}

void MainMenu::StartLauncherUpdate() {
    if (!HasLauncherUpdate()) {
        return;
    }

    auto* menu = this;
    const Assets assets = m_assets;
    auto error = std::make_shared<std::string>();
    auto restart = std::make_shared<bool>(false);

    App::Get().Push(std::make_unique<ProgressBox>(
        "Updating", "Launcher",
        [assets, error, restart](ProgressBox* pbox) {
            return InstallLauncher(assets, *pbox, *error, *restart);
        },
        [menu, error, restart](Result rc) {
            if (R_SUCCEEDED(rc) && *restart) {
                App::Get().Launch(LauncherPath());
                if (!App::Get().IsQuitting()) {
                    menu->ShowMessageLater("Launcher updated. Restart to apply.");
                }
                return;
            }
            if (R_SUCCEEDED(rc)) {
                menu->ShowMessageLater("Launcher updated. Restart to apply.");
            } else if (!error->empty()) {
                if (*error != "Install cancelled.") {
                    menu->ShowMessageLater(*error);
                }
            } else {
                menu->ShowMessageLater("Update failed, see launcher.log.");
            }
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

    auto sidebar = std::make_unique<Sidebar>("Game options", entry.name, Sidebar::Side::RIGHT);
    Sidebar* sidebar_ptr = sidebar.get();

    if (managed) {
        if (entry.state == GameState::Update) {
            sidebar->Add<SidebarEntryCallback>("Update", [menu, entry, game, sidebar_ptr] {
                menu->ConfirmInstall(entry, game, false, "Update", [sidebar_ptr] {
                    sidebar_ptr->SetPop();
                });
            });
        } else {
            sidebar->Add<SidebarEntryCallback>("Check for updates", [menu, entry, sidebar_ptr] {
                sidebar_ptr->SetPop();
                menu->CheckGameUpdate(entry);
            });
        }
        sidebar->Add<SidebarEntryCallback>("Reinstall", [menu, entry, game, sidebar_ptr] {
            menu->ConfirmInstall(entry, game, true, "Reinstall", [sidebar_ptr] {
                sidebar_ptr->SetPop();
            });
        });
    }

    sidebar->Add<SidebarEntryCallback>("Version info", [entry, sidebar_ptr] {
        sidebar_ptr->SetPop();
        std::string message = entry.name + "\n";
        message += "Game version: " + (entry.version.empty() ? "unknown" : entry.version) + "\n";
        message += "Recomp version: " +
                   (entry.recomp_version.empty() ? "unknown" : entry.recomp_version) + "\n";
        message += "Region: " + (entry.region.empty() ? "unknown" : entry.region);
        App::Get().Push(std::make_unique<OptionBox>(message, "Close"));
    });

    sidebar->Add<SidebarEntryCallback>("Verify files", [menu, entry, sidebar_ptr] {
        sidebar_ptr->SetPop();
        menu->StartVerify(entry);
    });

    sidebar->Add<SidebarEntryCallback>("Delete game", [menu, entry, sidebar_ptr] {
        App::Get().Push(std::make_unique<OptionBox>(
            "Delete " + entry.name +
                "?\nThe game files are removed. Save data, settings and disc images are kept.",
            "Cancel", "Delete", 0, [menu, entry, sidebar_ptr](std::optional<s64> choice) {
                if (choice && *choice) {
                    sidebar_ptr->SetPop();
                    menu->StartRemove(entry, true);
                }
            }));
    });

    sidebar->Add<SidebarEntryCallback>("Delete game and save data", [menu, entry, sidebar_ptr] {
        App::Get().Push(std::make_unique<OptionBox>(
            "Delete " + entry.name +
                " and its save data?\nThe game, saves and settings are removed. This cannot be undone.",
            "Cancel", "Delete", 0, [menu, entry, sidebar_ptr](std::optional<s64> choice) {
                if (choice && *choice) {
                    sidebar_ptr->SetPop();
                    menu->StartRemove(entry, false);
                }
            }));
    });

    App::Get().Push(std::move(sidebar));
}

void MainMenu::OpenDownloads() {
    App::Get().Push(std::make_unique<DownloadMenu>(
        &m_assets, [this](const GameEntry& entry) {
            return BannerFor(entry);
        }, [this] {
            RefreshBanners();
            Rebuild();
        }));
}

void MainMenu::OpenLauncherOptions() {
    auto* menu = this;
    auto sidebar = std::make_unique<Sidebar>("Settings", "v" + LauncherVersion(), Sidebar::Side::LEFT);
    Sidebar* sidebar_ptr = sidebar.get();

    sidebar->Add<SidebarEntryCallback>("Update all", [menu, sidebar_ptr] {
        sidebar_ptr->SetPop();
        menu->ConfirmUpdateAll();
    });

    sidebar->Add<SidebarEntryCallback>("Check launcher update", [menu, sidebar_ptr] {
        sidebar_ptr->SetPop();
        menu->CheckLauncherUpdate();
    });

    const auto local_catalogue = std::filesystem::path(RootDirectory()) / "assets.local.json";
    std::error_code local_ec;
    if (std::filesystem::is_regular_file(local_catalogue, local_ec)) {
        sidebar->Add<SidebarEntryCallback>("Clear local catalogue", [menu, local_catalogue, sidebar_ptr] {
            App::Get().Push(std::make_unique<OptionBox>(
                "Clear the local catalogue?\nassets.local.json is deleted and the cached or bundled catalogue is used.",
                "Cancel", "Clear", 0,
                [menu, local_catalogue, sidebar_ptr](std::optional<s64> choice) {
                    if (!choice || !*choice) {
                        return;
                    }
                    sidebar_ptr->SetPop();
                    std::error_code ec;
                    std::filesystem::remove(local_catalogue, ec);
                    if (ec) {
                        menu->ShowMessageLater("Could not delete assets.local.json.");
                        return;
                    }
                    LoadLocalAssets(menu->m_assets);
                    menu->RefreshBanners();
                    menu->Rebuild();
                    menu->ShowMessageLater("Local catalogue cleared.");
                }));
        });
    }

    sidebar->Add<SidebarEntryCallback>("About", [menu] {
        std::string problem;
        const BaseDataState state = CheckBaseData(problem);
        std::string base = state == BaseDataState::Ready ? "Ready" : "Missing";
        if (state != BaseDataState::Ready) {
            const bool dvd_root = std::any_of(menu->m_rows.begin(), menu->m_rows.end(),
                                             [](const GameEntry& entry) { return entry.base_ready; });
            if (dvd_root) {
                base = "Ready (dvd_root)";
            } else if (state == BaseDataState::Incomplete) {
                base = problem.empty() ? "Incomplete" : "Incomplete (" + problem + ")";
            }
        }

        std::string message = "NSMBW Launcher v" + LauncherVersion() + "\n";
        message += "Recomp version: " +
                   (menu->m_assets.recomp_version.empty() ? "unknown"
                                                         : menu->m_assets.recomp_version) +
                   "\n";
        message += "Installed games: " + std::to_string(menu->m_rows.size()) + "\n";
        message += "Catalogue games: " + std::to_string(menu->m_assets.games.size()) + "\n";
        message += "Base game data: " + base + "\n";
        message += "Launcher root: " + RootDirectory() + "\n";
        message += "Build: " __DATE__ "\n";
        message += "Credits: timschneeb, WitherBlitz, WiiCompiled, Aurora, Sphaira";
        App::Get().Push(std::make_unique<OptionBox>(message, "Close"));
    });

    App::Get().Push(std::move(sidebar));
}

void MainMenu::UpdateFooter() {
    std::string hint = "Launch";
    const auto games = static_cast<s64>(m_rows.size());
    if (m_index >= 0 && m_index < games) {
        const GameEntry& row = m_rows[static_cast<size_t>(m_index)];
        hint = row.installed && row.nro_valid ? "Launch" : "Details";
    } else if (ShowLauncherRow() && m_index == games) {
        hint = HasLauncherUpdate() ? "Install launcher update" : "Check launcher update";
    } else if (m_index >= games) {
        hint = "Open";
    }

    if (auto it = m_actions.find(Button::A); it != m_actions.end()) {
        it->second.m_hint = hint;
    }
    if (auto it = m_actions.find(Button::X); it != m_actions.end()) {
        it->second.m_hint = (m_index >= 0 && m_index < games) ? "Game options" : "";
    }
}

void MainMenu::Update(Controller* controller, TouchInfo* touch) {
    // Textures from a refresh are released two frames later, after the last draw that used them.
    if (!m_banner_discard_ready.empty() && App::GetVg() != nullptr) {
        for (int image : m_banner_discard_ready) {
            if (image > 0) {
                nvgDeleteImage(App::GetVg(), image);
            }
        }
        m_banner_discard_ready.clear();
    }
    m_banner_discard_ready = std::move(m_banner_discard_pending);
    m_banner_discard_pending.clear();

    m_list->OnUpdate(controller, touch, m_index, RowCount(), [this](bool touch, s64 i) {
        if (touch && m_index == i) {
            Activate(i);
            return;
        }

        m_index = i;
        m_scroll_name.Reset();
        m_scroll_author.Reset();
        UpdateFooter();
    });

    MenuBase::Update(controller, touch);
}

void MainMenu::Draw(NVGcontext* vg, Theme* theme) {
    MenuBase::Draw(vg, theme);

    m_list->Draw(vg, theme, RowCount(), [this](NVGcontext* vg, Theme* theme, const Vec4& v, s64 i) {
        const auto& [x, y, w, h] = v;

        const auto games = static_cast<s64>(m_rows.size());
        const bool launcher_row = ShowLauncherRow() && i == games;
        const bool download_row = !launcher_row && i >= games;

        std::string name;
        std::string subtitle;
        std::string meta;
        auto meta_id = ThemeEntryID_TEXT_INFO;
        bool update_pill = false;

        if (i < games) {
            const GameEntry& entry = m_rows[static_cast<size_t>(i)];
            name = entry.name;
            subtitle = entry.author;
            switch (entry.state) {
                case GameState::Install:
                    meta = "Install";
                    meta_id = ThemeEntryID_TEXT_SELECTED;
                    break;
                case GameState::Update:
                    update_pill = true;
                    meta = "Update available";
                    meta_id = ThemeEntryID_TEXT;
                    break;
                case GameState::Launch:
                    if (!entry.nro_valid) {
                        meta = "Invalid NRO";
                        meta_id = ThemeEntryID_ERROR;
                    } else if (!entry.base_ready) {
                        meta = "Needs game data";
                        meta_id = ThemeEntryID_ERROR;
                    } else {
                        meta = entry.recomp_version.empty() ? entry.version : entry.recomp_version;
                    }
                    break;
            }
        } else if (launcher_row) {
            name = "Install launcher update";
            if (HasLauncherUpdate() && !m_assets.launcher_version.empty()) {
                subtitle = "v" + LauncherVersion() + " -> v" + m_assets.launcher_version;
            } else {
                subtitle = "v" + LauncherVersion() + " (preview)";
            }
            update_pill = true;
            meta = "Update available";
            meta_id = ThemeEntryID_TEXT;
        } else {
            name = "Download community mods & games";
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
        float draw_w = 0.f;
        float draw_h = 0.f;
        int image = m_fallback_banner;
        if (i < games) {
            const auto banner = BannerFor(m_rows[static_cast<size_t>(i)]);
            image = banner.image;
            if (banner.width > 0 && banner.height > 0) {
                const float scale = std::min(kBannerBoxWidth / static_cast<float>(banner.width),
                                             banner_h / static_cast<float>(banner.height));
                draw_w = static_cast<float>(banner.width) * scale;
                draw_h = static_cast<float>(banner.height) * scale;
            }
        } else if (launcher_row) {
            const float icon_size = 56.f;
            const float icon_x = x + 15.f + (kBannerBoxWidth - icon_size) / 2.f;
            const float icon_y = y + 5.f + (banner_h - icon_size) / 2.f;
            gfx::drawRect(vg, Vec4{icon_x, icon_y, icon_size, icon_size},
                          theme->GetColour(ThemeEntryID_HIGHLIGHT_1), 14.f);
            const float cx = icon_x + icon_size / 2.f;
            const float top = icon_y + icon_size * 0.30f;
            const float bottom = icon_y + icon_size * 0.70f;
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx, bottom);
            nvgLineTo(vg, cx, top);
            nvgMoveTo(vg, cx - icon_size * 0.18f, top + icon_size * 0.18f);
            nvgLineTo(vg, cx, top);
            nvgLineTo(vg, cx + icon_size * 0.18f, top + icon_size * 0.18f);
            nvgStrokeColor(vg, theme->GetColour(ThemeEntryID_TEXT));
            nvgStrokeWidth(vg, 4.f);
            nvgLineCap(vg, NVG_ROUND);
            nvgLineJoin(vg, NVG_ROUND);
            nvgStroke(vg);
        } else if (download_row) {
            gfx::drawText(vg, x + 15.f + kBannerBoxWidth / 2.f, y + 5.f + banner_h / 2.f, 48.f,
                          theme->GetColour(ThemeEntryID_TEXT_SELECTED), "+",
                          NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        if (i < games) {
            if (draw_w <= 0.f || draw_h <= 0.f) {
                draw_w = banner_h * static_cast<float>(kLogoWidth) / static_cast<float>(kLogoHeight);
                draw_h = banner_h;
            }
            const float banner_x = x + 15.f + (kBannerBoxWidth - draw_w) / 2.f;
            const float banner_y = y + 5.f + (banner_h - draw_h) / 2.f;
            gfx::drawImage(vg, banner_x, banner_y, draw_w, draw_h, image, 2.f);
        }

        const float text_x = x + 15.f + kBannerBoxWidth + 15.f;
        const float text_w = w - (text_x - x) - 220.f;
        if (subtitle.empty()) {
            m_scroll_name.Draw(vg, selected, text_x, y + (h / 2.f), text_w, 20.f,
                               NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), name);
        } else {
            const auto author_id = selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT_INFO;
            m_scroll_name.Draw(vg, selected, text_x, y + (h / 2.f) - 13.f, text_w, 20.f,
                               NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(text_id), name);
            m_scroll_author.Draw(vg, selected, text_x, y + (h / 2.f) + 13.f, text_w, 16.f,
                                 NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE,
                                 theme->GetColour(author_id), subtitle);
        }

        if (update_pill) {
            float bounds[4]{};
            nvgFontSize(vg, 16.f);
            gfx::textBounds(vg, 0, 0, bounds, meta.c_str());
            const float pill_w = std::max(78.f, bounds[2] - bounds[0] + 24.f);
            const float pill_h = 26.f;
            const float pill_x = x + w - 15.f - pill_w;
            const float pill_y = y + (h - pill_h) / 2.f;
            gfx::drawRect(vg, Vec4{pill_x, pill_y, pill_w, pill_h},
                          theme->GetColour(ThemeEntryID_HIGHLIGHT_1), pill_h / 2.f);
            gfx::drawText(vg, pill_x + pill_w / 2.f, pill_y + pill_h / 2.f, 16.f,
                          theme->GetColour(ThemeEntryID_TEXT), meta.c_str(),
                          NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        } else {
            gfx::drawText(vg, x + w - 15.f, y + (h / 2.f), 16.f, theme->GetColour(meta_id),
                          meta.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
    });
}

} // namespace launcher::ui
