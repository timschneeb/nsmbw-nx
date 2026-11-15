#include "log.hpp"

#include "paths.hpp"

#include <switch.h>

#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace launcher {
namespace {
std::string g_log_path = "launcher.log";
std::mutex g_mutex;
}

void log_init(const char* argv0) {
    (void)argv0;
    g_log_path = (std::filesystem::path(RootDirectory()) / "launcher.log").string();
    // Truncate once, then append per line so the file is never held open and
    // can be read over FTP while the launcher is running.
    FILE* file = std::fopen(g_log_path.c_str(), "w");
    if (file != nullptr) {
        std::fclose(file);
    }
}

void log_write(const char* format, ...) {
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    std::lock_guard<std::mutex> lock(g_mutex);
    FILE* file = std::fopen(g_log_path.c_str(), "a");
    if (file != nullptr) {
        std::fputs(line, file);
        std::fclose(file);
    }
    std::fputs(line, stdout);
    std::fflush(stdout);
}

void log_close() {
}
} // namespace launcher
