#pragma once

#include "assets.hpp"
#include "games.hpp"

#include <switch.h>

#include <string>

namespace launcher {

namespace ui { struct ProgressBox; }

Result InstallGame(const GameEntry& entry, const AssetsGame& catalog, ui::ProgressBox& pbox, bool force = false);
Result InstallLauncher(const Assets& assets, ui::ProgressBox& pbox);
void CleanupPendingInstalls();

} // namespace launcher
