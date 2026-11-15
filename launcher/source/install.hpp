#pragma once

#include "assets.hpp"
#include "games.hpp"

#include <switch.h>

#include <string>

namespace launcher {

namespace ui { struct ProgressBox; }

Result InstallGame(const GameEntry& entry, const AssetsGame& catalog, ui::ProgressBox& pbox,
                   std::string& error, bool force = false);
Result VerifyGame(const GameEntry& entry, ui::ProgressBox& pbox, std::string& error);
Result InstallLauncher(const Assets& assets, ui::ProgressBox& pbox, std::string& error, bool& restart);
void CleanupPendingInstalls();

// Bytes transferred when every component has to be downloaded or copied.
auto InstallDownloadSize(const AssetsGame& game) -> s64;
// Bytes actually transferred for this entry and action.
auto InstallTransferSize(const GameEntry& entry, const AssetsGame& game, bool force = false) -> s64;
// Free space the install needs at its peak, including the files it replaces.
auto InstallRequiredSpace(const GameEntry& entry, const AssetsGame& game, bool force = false) -> s64;
// Confirmation text with download size and free space.
auto InstallPromptMessage(const GameEntry& entry, const AssetsGame& game, const std::string& action,
                          bool force = false) -> std::string;
auto FormatSize(s64 bytes) -> std::string;

} // namespace launcher
