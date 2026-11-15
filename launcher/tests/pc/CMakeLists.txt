cmake_minimum_required(VERSION 3.21)
project(launcher_pc_tests LANGUAGES C CXX)

enable_testing()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.12.0
)
set(YYJSON_INSTALL OFF)
FetchContent_MakeAvailable(yyjson)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG master
)
FetchContent_GetProperties(stb)
if(NOT stb_POPULATED)
    FetchContent_Populate(stb)
endif()

FetchContent_Declare(nanovg_upstream
    GIT_REPOSITORY https://github.com/memononen/nanovg.git
    GIT_TAG master
)
FetchContent_GetProperties(nanovg_upstream)
if(NOT nanovg_upstream_POPULATED)
    FetchContent_Populate(nanovg_upstream)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
find_package(OpenSSL REQUIRED)
find_package(OpenGL REQUIRED)
pkg_check_modules(EPOXY REQUIRED IMPORTED_TARGET epoxy)
pkg_check_modules(GLFW IMPORTED_TARGET glfw3)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)

add_executable(launcher_pc_tests
    test_main.cpp
    test_shim.cpp
    ../../source/paths.cpp
    ../../source/log.cpp
    ../../source/sha.cpp
    ../../source/http.cpp
    ../../source/assets.cpp
    ../../source/games.cpp
    ../../source/install.cpp
    ../../source/romfs.cpp
)

target_include_directories(launcher_pc_tests PRIVATE
    shim
    ../../source
)

target_compile_definitions(launcher_pc_tests PRIVATE
    LAUNCHER_TEST=1
    LAUNCHER_VERSION="test"
    ASSETS_URL="file:///dev/null"
)

target_compile_options(launcher_pc_tests PRIVATE -Wall -Wextra)
target_link_libraries(launcher_pc_tests PRIVATE
    yyjson
    PkgConfig::CURL
    PkgConfig::ZSTD
    OpenSSL::Crypto
)

add_test(NAME launcher_pc_tests COMMAND launcher_pc_tests)

add_executable(launcher_ui_tests
    test_ui.cpp
    test_app.cpp
    pc_app_common.cpp
    basedata_stub.cpp
    nvg_stubs.cpp
    test_shim.cpp
    ../../source/paths.cpp
    ../../source/log.cpp
    ../../source/sha.cpp
    ../../source/http.cpp
    ../../source/assets.cpp
    ../../source/games.cpp
    ../../source/install.cpp
    ../../source/romfs.cpp
    ../../source/menu_main.cpp
    ../../source/ui/widget.cpp
    ../../source/ui/list.cpp
    ../../source/ui/scrolling_text.cpp
    ../../source/ui/nvg_util.cpp
    ../../source/ui/menus/menu_base.cpp
    ../../source/ui/menus/menu_download.cpp
    ../../source/ui/option_box.cpp
    ../../source/ui/sidebar.cpp
)

target_include_directories(launcher_ui_tests PRIVATE
    shim
    ../../source
)

target_compile_definitions(launcher_ui_tests PRIVATE
    LAUNCHER_TEST=1
    LAUNCHER_TEST_UI=1
    LAUNCHER_VERSION="test"
    ASSETS_URL="file:///dev/null"
    LAUNCHER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

target_compile_options(launcher_ui_tests PRIVATE -Wall -Wextra)
target_link_libraries(launcher_ui_tests PRIVATE
    yyjson
    PkgConfig::CURL
    PkgConfig::ZSTD
    OpenSSL::Crypto
)

add_test(NAME launcher_ui_tests COMMAND launcher_ui_tests)

add_executable(launcher_network_tests
    test_network.cpp
    test_http_server.cpp
    test_shim.cpp
    ../../source/paths.cpp
    ../../source/log.cpp
    ../../source/sha.cpp
    ../../source/http.cpp
    ../../source/assets.cpp
    ../../source/games.cpp
    ../../source/install.cpp
    ../../source/romfs.cpp
)

target_include_directories(launcher_network_tests PRIVATE
    shim
    ../../source
)

target_compile_definitions(launcher_network_tests PRIVATE
    LAUNCHER_TEST=1
    LAUNCHER_VERSION="test"
    ASSETS_URL="file:///dev/null"
)

target_compile_options(launcher_network_tests PRIVATE -Wall -Wextra)
target_link_libraries(launcher_network_tests PRIVATE
    yyjson
    PkgConfig::CURL
    PkgConfig::ZSTD
    OpenSSL::Crypto
)

add_test(NAME launcher_network_tests COMMAND launcher_network_tests)

add_executable(launcher_scenario_tests
    test_scenarios.cpp
    test_http_server.cpp
    test_app.cpp
    pc_app_common.cpp
    basedata_stub.cpp
    nvg_stubs.cpp
    test_shim.cpp
    ../../source/paths.cpp
    ../../source/log.cpp
    ../../source/sha.cpp
    ../../source/http.cpp
    ../../source/assets.cpp
    ../../source/games.cpp
    ../../source/install.cpp
    ../../source/romfs.cpp
    ../../source/menu_main.cpp
    ../../source/ui/widget.cpp
    ../../source/ui/list.cpp
    ../../source/ui/scrolling_text.cpp
    ../../source/ui/nvg_util.cpp
    ../../source/ui/menus/menu_base.cpp
    ../../source/ui/menus/menu_download.cpp
    ../../source/ui/option_box.cpp
    ../../source/ui/sidebar.cpp
)

target_include_directories(launcher_scenario_tests PRIVATE
    .
    shim
    ../../source
)

target_compile_definitions(launcher_scenario_tests PRIVATE
    LAUNCHER_TEST=1
    LAUNCHER_TEST_UI=1
    LAUNCHER_VERSION="test"
    ASSETS_URL="file:///dev/null"
    LAUNCHER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

target_compile_options(launcher_scenario_tests PRIVATE -Wall -Wextra)
target_link_libraries(launcher_scenario_tests PRIVATE
    yyjson
    PkgConfig::CURL
    PkgConfig::ZSTD
    OpenSSL::Crypto
)

add_test(NAME launcher_scenario_tests COMMAND launcher_scenario_tests)

add_library(nanovg_pc STATIC
    nanovg_gl_impl.cpp
    "${nanovg_upstream_SOURCE_DIR}/src/nanovg.c"
)
target_include_directories(nanovg_pc PUBLIC
    "${nanovg_upstream_SOURCE_DIR}/src"
    "${stb_SOURCE_DIR}"
)
target_link_libraries(nanovg_pc PUBLIC
    PkgConfig::EPOXY
    PkgConfig::EGL
    OpenGL::GL
)

add_executable(launcher_render
    render_main.cpp
    render_app.cpp
    demo_fixtures.cpp
    pc_app_common.cpp
    basedata_stub.cpp
    test_shim.cpp
    ../../source/paths.cpp
    ../../source/log.cpp
    ../../source/sha.cpp
    ../../source/http.cpp
    ../../source/assets.cpp
    ../../source/games.cpp
    ../../source/install.cpp
    ../../source/romfs.cpp
    ../../source/menu_main.cpp
    ../../source/ui/widget.cpp
    ../../source/ui/list.cpp
    ../../source/ui/scrolling_text.cpp
    ../../source/ui/nvg_util.cpp
    ../../source/ui/menus/menu_base.cpp
    ../../source/ui/menus/menu_download.cpp
    ../../source/ui/option_box.cpp
    ../../source/ui/sidebar.cpp
)

target_include_directories(launcher_render PRIVATE
    shim
    ../../source
    "${nanovg_upstream_SOURCE_DIR}/src"
    "${stb_SOURCE_DIR}"
)

target_compile_definitions(launcher_render PRIVATE
    LAUNCHER_TEST=1
    LAUNCHER_TEST_UI=1
    LAUNCHER_VERSION="test"
    ASSETS_URL="file:///dev/null"
    LAUNCHER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

target_compile_options(launcher_render PRIVATE -Wall -Wextra)
target_link_libraries(launcher_render PRIVATE
    yyjson
    nanovg_pc
    PkgConfig::CURL
    PkgConfig::ZSTD
    OpenSSL::Crypto
)

add_test(NAME launcher_render COMMAND launcher_render)

if(GLFW_FOUND)
add_executable(launcher_viewer
    viewer_main.cpp
    viewer_app.cpp
    demo_fixtures.cpp
    pc_app_common.cpp
    basedata_stub.cpp
    test_shim.cpp
    ../../source/paths.cpp
    ../../source/log.cpp
    ../../source/sha.cpp
    ../../source/http.cpp
    ../../source/assets.cpp
    ../../source/games.cpp
    ../../source/install.cpp
    ../../source/romfs.cpp
    ../../source/menu_main.cpp
    ../../source/ui/widget.cpp
    ../../source/ui/list.cpp
    ../../source/ui/scrolling_text.cpp
    ../../source/ui/nvg_util.cpp
    ../../source/ui/menus/menu_base.cpp
    ../../source/ui/menus/menu_download.cpp
    ../../source/ui/option_box.cpp
    ../../source/ui/sidebar.cpp
)

target_include_directories(launcher_viewer PRIVATE
    .
    shim
    ../../source
    "${nanovg_upstream_SOURCE_DIR}/src"
    "${stb_SOURCE_DIR}"
)

target_compile_definitions(launcher_viewer PRIVATE
    LAUNCHER_TEST=1
    LAUNCHER_TEST_UI=1
    LAUNCHER_VERSION="test"
    ASSETS_URL="file:///dev/null"
    LAUNCHER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

target_compile_options(launcher_viewer PRIVATE -Wall -Wextra)
target_link_libraries(launcher_viewer PRIVATE
    yyjson
    nanovg_pc
    PkgConfig::GLFW
    OpenGL::GL
    PkgConfig::CURL
    PkgConfig::ZSTD
    OpenSSL::Crypto
)
endif()
