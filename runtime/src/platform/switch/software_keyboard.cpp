#include "software_keyboard.h"

#if defined(MKW_PLATFORM_SWITCH)
#include <switch.h>

#include <array>

std::string HostAskText(const std::string& header, const std::string& guide, const std::string& initial) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) {
        return {};
    }
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetType(&kbd, SwkbdType_QWERTY);
    swkbdConfigSetHeaderText(&kbd, header.c_str());
    swkbdConfigSetGuideText(&kbd, guide.c_str());
    if (!initial.empty()) {
        swkbdConfigSetInitialText(&kbd, initial.c_str());
    }
    swkbdConfigSetStringLenMax(&kbd, 64);

    std::array<char, 65> text{};
    const Result rc = swkbdShow(&kbd, text.data(), text.size());
    swkbdClose(&kbd);
    return R_SUCCEEDED(rc) ? std::string(text.data()) : std::string();
}
#endif
