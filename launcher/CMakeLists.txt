cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not defined. export DEVKITPRO=/opt/devkitpro")
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{DEVKITPRO}/cmake/Switch.cmake")
endif()

project(nsmbw_launcher VERSION 1.0.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-O2 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti)
add_link_options(-Wl,--gc-sections)

include(FetchContent)
FetchContent_Declare(nanovg
    GIT_REPOSITORY https://github.com/ITotalJustice/nanovg-deko3d.git
    GIT_TAG 2acb0f3
)
set(NANOVG_NO_JPEG OFF)
set(NANOVG_NO_PNG OFF)
set(NANOVG_NO_BMP OFF)
set(NANOVG_NO_PSD ON)
set(NANOVG_NO_TGA ON)
set(NANOVG_NO_GIF ON)
set(NANOVG_NO_HDR ON)
set(NANOVG_NO_PIC ON)
set(NANOVG_NO_PNM ON)
FetchContent_MakeAvailable(nanovg)

FetchContent_Declare(yyjson
    GIT_REPOSITORY https://github.com/ibireme/yyjson.git
    GIT_TAG 0.12.0
)
set(YYJSON_INSTALL OFF)
FetchContent_MakeAvailable(yyjson)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)

set(MKW_LOGO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src/platform/switch/nsmbw_logo.rgba")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/source/nsmbw_logo.S.in"
    "${CMAKE_BINARY_DIR}/nsmbw_logo.S"
    @ONLY)

add_executable(launcher
    "${CMAKE_BINARY_DIR}/nsmbw_logo.S"
    source/main.cpp
    source/app.cpp
    source/log.cpp
    source/assets.cpp
    source/games.cpp
    source/http.cpp
    source/install.cpp
    source/paths.cpp
    source/sha.cpp
    source/romfs.cpp
    source/menu_main.cpp
    source/ui/widget.cpp
    source/ui/list.cpp
    source/ui/scrolling_text.cpp
    source/ui/nvg_util.cpp
    source/ui/menus/menu_base.cpp
    source/ui/progress_box.cpp
    source/ui/option_box.cpp
    source/ui/sidebar.cpp
)

target_include_directories(launcher PRIVATE
    source
    ${CMAKE_CURRENT_SOURCE_DIR}/../runtime/include
)

target_compile_definitions(launcher PRIVATE
    LAUNCHER_VERSION="${PROJECT_VERSION}"
    ASSETS_URL="https://github.com/timschneeb/nsmbw-nx/releases/latest/download/assets.json"
)

target_link_libraries(launcher PRIVATE
    nanovg
    yyjson
    PkgConfig::CURL
    PkgConfig::ZSTD
    deko3d
    nx
)

find_program(NX_NACPTOOL NAMES nacptool HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
find_program(NX_ELF2NRO NAMES elf2nro HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)

set(NACP "${CMAKE_BINARY_DIR}/launcher.nacp")
execute_process(
    COMMAND "${NX_NACPTOOL}" --create "NSMBW Launcher" "WiiCompiled" "1.0.0" "${NACP}"
    RESULT_VARIABLE NACP_RESULT)
if(NOT NACP_RESULT EQUAL 0)
    message(FATAL_ERROR "nacptool failed")
endif()

add_custom_command(TARGET launcher POST_BUILD
    COMMAND "${NX_ELF2NRO}" "$<TARGET_FILE:launcher>" "${CMAKE_BINARY_DIR}/launcher.nro"
        "--nacp=${NACP}"
        "--icon=${CMAKE_CURRENT_SOURCE_DIR}/../runtime/assets/icon/switch.jpg"
    COMMENT "Packing launcher.nro"
    VERBATIM)
