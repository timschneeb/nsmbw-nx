#include "log.hpp"

#include <switch.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace launcher {
namespace {
constexpr const char* kLogPath = "sdmc:/switch/nsmbw/launcher.log";
std::mutex g_mutex;
}

void log_init(const char* argv0) {
    (void)argv0;
    // Truncate once, then append per line so the file is never held open and
    // can be read over FTP while the launcher is running.
    FILE* file = std::fopen(kLogPath, "w");
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
    FILE* file = std::fopen(kLogPath, "a");
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
