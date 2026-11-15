#include "json_util.hpp"

#include <cstdio>
#include <string>

namespace {

auto Expect(bool condition, const char* what) -> bool {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
    }
    return condition;
}

} // namespace

int main() {
    bool ok = true;

    ok &= Expect(launcher::IsSafeId("vanilla"), "safe id");
    ok &= Expect(launcher::IsSafeId("newer-smbw_1.30"), "safe id with punctuation");
    ok &= Expect(!launcher::IsSafeId(""), "empty id");
    ok &= Expect(!launcher::IsSafeId(".."), "parent id");
    ok &= Expect(!launcher::IsSafeId("../escape"), "traversal id");
    ok &= Expect(!launcher::IsSafeId("a/b"), "slash id");
    ok &= Expect(!launcher::IsSafeId("a\\b"), "backslash id");

    ok &= Expect(launcher::JsonEscape("plain") == "plain", "plain escape");
    ok &= Expect(launcher::JsonEscape("a\"b") == "a\\\"b", "quote escape");
    ok &= Expect(launcher::JsonEscape("a\\b") == "a\\\\b", "backslash escape");
    ok &= Expect(launcher::JsonEscape("a\nb") == "a\\nb", "newline escape");
    ok &= Expect(launcher::JsonEscape(std::string("a\x01" "b", 3)) == "a\\u0001b", "control escape");

    ok &= Expect(launcher::Trim("  value\r\n") == "value", "trim");
    return ok ? 0 : 1;
}
