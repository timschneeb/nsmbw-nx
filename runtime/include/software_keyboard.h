#pragma once

#include <string>

// The host's own text prompt: the Switch software keyboard. Empty when the player cancels or the
// applet cannot be shown. Android asks through its app, and desktop has a hardware keyboard.
#if defined(MKW_PLATFORM_SWITCH)
std::string HostAskText(const std::string& header, const std::string& guide, const std::string& initial);
#endif
