#pragma once

#include "app.hpp"

struct NVGcontext;

namespace launcher::pc {

auto CurrentApp() -> App*&;
void LoadLogo();
void LoadFonts(NVGcontext* vg);
void LoadTheme(Theme& theme);

} // namespace launcher::pc
