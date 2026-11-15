#pragma once

namespace launcher::ui {

struct BannerImage {
    int image{};
    int width{};
    int height{};
    long long file_size{};
    long long mtime{};
};

} // namespace launcher::ui
