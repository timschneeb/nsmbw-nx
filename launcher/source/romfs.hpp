#pragma once

#include "assets.hpp"

#include <switch.h>

#include <string>

namespace launcher {

namespace ui { struct ProgressBox; }

// Downloads or reads a zstd-compressed RomFS image and writes the uncompressed
// image to dest.
Result InstallRomfs(const Component& component, const std::string& dest, ui::ProgressBox& pbox,
                    std::string& error, bool commit = true);

} // namespace launcher
