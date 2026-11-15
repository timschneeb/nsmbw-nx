#pragma once

#include <string>

namespace launcher {

// Uses the directory of the running launcher.nro. Empty input falls back to the current directory.
void SetRootDirectory(const std::string& argv0);

const std::string& RootDirectory();
const std::string& LauncherPath();
const std::string& GamesDirectory();
const std::string& BannersDirectory();
const std::string& DiscsDirectory();
const std::string& BaseDiscDirectory();
const std::string& LauncherVersion();

} // namespace launcher
