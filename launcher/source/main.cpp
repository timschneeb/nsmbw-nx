#include "app.hpp"
#include "paths.hpp"

#include <switch.h>

#include <cstdio>

namespace {

constexpr const char* kAppletModeMessage =
    "Launch the NSMBW launcher from hbmenu with full memory access. Start any game title on the "
    "home menu while holding R.\n\nFor easier access, you can also use Sphaira to create a home "
    "menu forwarder for the launcher.";

} // namespace

int main(int argc, char** argv) {
    const AppletType appletType = appletGetAppletType();
    if (appletType != AppletType_Application && appletType != AppletType_SystemApplication) {
        ErrorSystemConfig config{};
        if (R_SUCCEEDED(errorSystemCreate(&config, kAppletModeMessage, nullptr))) {
            errorSystemShow(&config);
        } else {
            std::fprintf(stderr, "%s\n", kAppletModeMessage);
        }
        return 0;
    }

    launcher::SetRootDirectory(argv != nullptr && argv[0] != nullptr ? argv[0] : "");
    launcher::App app(argc, argv);
    app.Run();
    return 0;
}
