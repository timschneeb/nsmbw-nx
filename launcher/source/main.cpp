#include "app.hpp"
#include "paths.hpp"

int main(int argc, char** argv) {
    launcher::SetRootDirectory(argv != nullptr && argv[0] != nullptr ? argv[0] : "");
    launcher::App app(argc, argv);
    app.Run();
    return 0;
}
