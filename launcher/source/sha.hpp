#pragma once

#include <switch.h>

#include <string>

namespace launcher {

Result FileSha256(const std::string& path, std::string& hex);

} // namespace launcher
