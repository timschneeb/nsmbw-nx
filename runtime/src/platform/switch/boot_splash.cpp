#include <switch.h>

#include <cstdio>
#include <cstring>

namespace {
PrintConsole* g_console = nullptr;

void PrintCentered(const PrintConsole* con, const char* text) {
    const int width = con != nullptr ? con->consoleWidth : 80;
    const int length = static_cast<int>(std::strlen(text));
    int padding = (width - length) / 2;
    if (padding < 0) {
        padding = 0;
    }
    std::printf("%*s%s\n", padding, "", text);
}
} // namespace

// Shown before the process transcript takes stdout: consoleInit points stdout at the text
// console, so these lines are drawn before InitializeProcessTranscript redirects it to the log.
extern "C" void NsmbwBootSplashShow() {
    if (g_console != nullptr) {
        return;
    }
    PrintConsole* con = consoleInit(nullptr);
    if (con == nullptr) {
        return;
    }
    g_console = con;
    consoleClear();
    std::printf("\n\n\n");
    PrintCentered(con, "New Super Mario Bros. Wii");
    std::printf("\n");
    PrintCentered(con, "Initializing...");
    std::printf("\n");
    PrintCentered(con, "This can take a minute on first start.");
    consoleUpdate(con);
}

// Called right before the first swapchain configuration, which takes over the default NWindow.
extern "C" void NsmbwBootSplashHide() {
    if (g_console == nullptr) {
        return;
    }
    PrintConsole* con = g_console;
    g_console = nullptr;
    consoleExit(con);
}
