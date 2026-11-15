#include "version.hpp"

#include <cassert>
#include <iostream>

int main() {
    using launcher::CompareVersion;

    assert(CompareVersion("1.0.0", "1.0.0") == 0);
    assert(CompareVersion("1.0", "1.0.0") == 0);
    assert(CompareVersion("1.30", "1.9") > 0);
    assert(CompareVersion("1.0.1", "1.0.0") > 0);
    assert(CompareVersion("1.2", "1.1") > 0);
    assert(CompareVersion("", "1") < 0);
    assert(CompareVersion("2.0", "10.0") < 0);

    std::cout << "compare_version ok\n";
    return 0;
}
