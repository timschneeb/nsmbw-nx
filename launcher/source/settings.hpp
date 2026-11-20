#pragma once

namespace launcher {

enum class DisplayStyle {
    Detail,
    BannerGrid,
    GridDetail,
};

struct LauncherSettings {
    DisplayStyle display_style{DisplayStyle::Detail};
    bool show_download_row{true};
};

auto ReadSettings() -> LauncherSettings;
void WriteSettings(const LauncherSettings& settings);
auto DisplayStyleName(DisplayStyle style) -> const char*;

} // namespace launcher
