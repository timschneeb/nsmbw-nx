#include "basedata.hpp"

#include "games.hpp"
#include "test_hooks.hpp"
#include "ui/progress_box.hpp"

namespace launcher {

auto BaseDataReady() -> bool { return test::g_base_ready; }

auto BaseDataReadyFor(const GameEntry&) -> bool { return test::g_base_ready_for; }

auto CheckBaseData(std::string& problem) -> BaseDataState {
    problem = test::g_base_problem;
    return test::g_base_state;
}

auto FindBaseImage(std::string& problem) -> std::optional<BaseImage> {
    problem = test::g_base_image_problem;
    if (!test::g_base_has_image) {
        return std::nullopt;
    }
    return test::g_base_image;
}

Result InstallBaseImage(const BaseImage&, ui::ProgressBox& pbox, std::string& error,
                        bool) {
    if (pbox.ShouldExit()) {
        error = "Base data extraction cancelled.";
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    if (!test::g_base_install_ok) {
        error = test::g_base_install_error;
        return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    }
    test::g_base_state = BaseDataState::Ready;
    test::g_base_ready = true;
    test::g_base_ready_for = true;
    test::g_base_has_image = false;
    return 0;
}

void CleanupBaseImport() {}

} // namespace launcher
