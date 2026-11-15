#pragma once

#include <cstdio>
#include <string>

namespace launcher {

// Catalogue ids become directory and file names, so keep them to a conservative set.
inline auto IsSafeId(const std::string& id) -> bool {
    if (id.empty() || id == "." || id == ".." || id.front() == '.') {
        return false;
    }
    for (const unsigned char c : id) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

inline auto JsonEscape(const std::string& value) -> std::string {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char escaped[7];
                    std::snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    out += escaped;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

inline auto Trim(std::string value) -> std::string {
    size_t begin = 0;
    while (begin < value.size() && (value[begin] == ' ' || value[begin] == '\t' ||
                                    value[begin] == '\r' || value[begin] == '\n')) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\t' ||
                           value[end - 1] == '\r' || value[end - 1] == '\n')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

} // namespace launcher
