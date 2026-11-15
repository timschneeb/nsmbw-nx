#pragma once

#include <switch.h>

#include <atomic>

#include <functional>
#include <string>
#include <vector>

namespace launcher::http {

using Progress = std::function<bool(s64 now, s64 total)>;
using DataCallback = std::function<bool(const u8* data, size_t size)>;

bool Init();
void Exit();

Result Download(const std::string& url, const std::string& path, const Progress& progress);
Result Get(const std::string& url, std::vector<u8>& out, const std::atomic_bool* cancel = nullptr);
Result DownloadStream(const std::string& url, const DataCallback& on_data, const Progress& progress);

} // namespace launcher::http
