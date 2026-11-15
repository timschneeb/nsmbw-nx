#pragma once

// Test-only controls for the PC harness. These are inline so every test executable gets its own
// copy and there is no extra translation unit to link.

#include "basedata.hpp"

#include <switch.h>

#include <string>
#include <utility>

namespace launcher::test {

struct SdSizeOverride {
    bool fail{};
    s64 free{-1};
    s64 total{-1};
};

inline SdSizeOverride g_sd_size{};

inline bool g_base_ready{true};
inline bool g_base_ready_for{true};
inline BaseDataState g_base_state{BaseDataState::Ready};
inline std::string g_base_problem{};
inline bool g_base_has_image{false};
inline BaseImage g_base_image{};
inline std::string g_base_image_problem{};
inline bool g_base_install_ok{true};
inline std::string g_base_install_error{"Base data extraction failed."};

inline void Reset() {
    g_sd_size = {};
    g_base_ready = true;
    g_base_ready_for = true;
    g_base_state = BaseDataState::Ready;
    g_base_problem.clear();
    g_base_has_image = false;
    g_base_image = {};
    g_base_image_problem.clear();
    g_base_install_ok = true;
    g_base_install_error = "Base data extraction failed.";
}

inline void SetBaseState(BaseDataState state, std::string problem = {}) {
    g_base_state = state;
    g_base_problem = std::move(problem);
    g_base_ready = state == BaseDataState::Ready;
    g_base_ready_for = g_base_ready;
}

inline void SetBaseImage(std::string path, std::string id = "SMNE01", int revision = 1,
                         s64 extracted_size = 1024) {
    g_base_has_image = true;
    g_base_image.path = std::move(path);
    g_base_image.id = std::move(id);
    g_base_image.revision = revision;
    g_base_image.extracted_size = extracted_size;
    g_base_image.image_size = extracted_size;
}

} // namespace launcher::test
