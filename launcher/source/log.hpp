#pragma once

#include <cstdarg>
#include <cstdio>

namespace launcher {
void log_init(const char* argv0);
void log_write(const char* format, ...);
void log_close();
} // namespace launcher
