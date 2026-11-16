#pragma once

#include <switch.h>

#include <functional>
#include <string>
#include <vector>

namespace launcher::http {

using Progress = std::function<bool(s64 now, s64 total)>;
using DataCallback = std::function<bool(const u8* data, size_t size)>;
using Cancel = std::function<bool()>;

bool Init();
void Exit();

Result Download(const std::string& url, const std::string& path, const Progress& progress,
                s64 expected_size = 0, int connections = 0);
Result Get(const std::string& url, std::vector<u8>& out, const Cancel& cancel = {},
           size_t max_size = 16 * 1024 * 1024);
Result DownloadStream(const std::string& url, const DataCallback& on_data, const Progress& progress);

} // namespace launcher::http
