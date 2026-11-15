#pragma once

#include <cctype>
#include <string>

namespace launcher {

// Numeric segment comparison: 1.30 is newer than 1.9, missing segments count as 0.
inline auto CompareVersion(const std::string& a, const std::string& b) -> int {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() || j < b.size()) {
        long left = 0;
        long right = 0;
        while (i < a.size() && std::isdigit(static_cast<unsigned char>(a[i]))) {
            left = left * 10 + (a[i] - '0');
            i++;
        }
        while (j < b.size() && std::isdigit(static_cast<unsigned char>(b[j]))) {
            right = right * 10 + (b[j] - '0');
            j++;
        }
        if (left != right) {
            return left < right ? -1 : 1;
        }
        while (i < a.size() && !std::isdigit(static_cast<unsigned char>(a[i]))) {
            i++;
        }
        while (j < b.size() && !std::isdigit(static_cast<unsigned char>(b[j]))) {
            j++;
        }
    }
    return 0;
}

} // namespace launcher
