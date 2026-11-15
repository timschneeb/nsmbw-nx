#pragma once

#include <switch.h>

#include <functional>
#include <string>

namespace launcher {

using ShaProgress = std::function<void(s64 now, s64 total)>;

Result FileSha256(const std::string& path, std::string& hex, const std::function<bool()>& canceled = {},
                  const ShaProgress& progress = {});

} // namespace launcher
