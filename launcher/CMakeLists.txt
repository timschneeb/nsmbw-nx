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

add_compile_options(-O2 -ffunction-sections -fdata-sections)
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions> $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>)
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
find_library(BZ2_LIBRARY NAMES bz2 REQUIRED)

set(NSMBW_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../runtime")
file(GLOB NSMBW_CRYPTOPP_SOURCES CONFIGURE_DEPENDS
    "${NSMBW_RUNTIME_DIR}/third_party/cryptopp/*.cpp")
list(FILTER NSMBW_CRYPTOPP_SOURCES EXCLUDE REGEX
    "/(adhoc|bench[123]|datatest|dlltest|fipsalgt|fipstest|pch|regtest[1234]|simple|test|validat[0-9]+)\\.cpp$")
list(FILTER NSMBW_CRYPTOPP_SOURCES EXCLUDE REGEX
    "(_avx|_ppc|_simd|_sse)\\.cpp$")

# The same disc reader and decoder sources the runtime uses, compiled into the
# launcher so the base game data can be extracted before any game runs.
add_library(nsmbw_disc STATIC
    "${NSMBW_RUNTIME_DIR}/src/wii_disc.cpp"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/7zArcIn.c"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/7zBuf.c"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/7zCrc.c"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/7zCrcOpt.c"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/7zDec.c"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/7zStream.c"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/Bcj2.c"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/CpuArch.c"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/LzmaDec.c"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk/Lzma2Dec.c"
    ${NSMBW_CRYPTOPP_SOURCES}
    "${NSMBW_RUNTIME_DIR}/third_party/cryptopp/rijndael_simd.cpp"
)
target_include_directories(nsmbw_disc PRIVATE
    "${NSMBW_RUNTIME_DIR}/include"
    "${NSMBW_RUNTIME_DIR}/third_party"
    "${NSMBW_RUNTIME_DIR}/third_party/lzma-sdk"
)
target_compile_definitions(nsmbw_disc PRIVATE
    MKW_PLATFORM_SWITCH=1
    CRYPTOPP_DISABLE_ASM
    CRYPTOPP_NO_GLOBAL_BYTE
    CRYPTOPP_ARM_AES_AVAILABLE=1
    Z7_NO_METHODS_FILTERS
)
target_compile_options(nsmbw_disc PRIVATE -w -fexceptions -frtti)
set_source_files_properties("${NSMBW_RUNTIME_DIR}/third_party/cryptopp/rijndael_simd.cpp"
    PROPERTIES COMPILE_DEFINITIONS "CRYPTOPP_ARM_NEON_AVAILABLE=1;CRYPTOPP_ARM_NEON_HEADER=1")
target_link_libraries(nsmbw_disc PUBLIC PkgConfig::ZSTD ${BZ2_LIBRARY})

set(MKW_LOGO_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/src/platform/switch/nsmbw_logo.rgba")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/source/nsmbw_logo.S.in"
    "${CMAKE_BINARY_DIR}/nsmbw_logo.S"
    @ONLY)

find_program(NX_BIN2S NAMES bin2s HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
set(LAUNCHER_CACERT "${CMAKE_CURRENT_SOURCE_DIR}/../runtime/assets/certs/cacert.pem")
if(NOT EXISTS "${LAUNCHER_CACERT}")
    message(FATAL_ERROR "Missing CA bundle: ${LAUNCHER_CACERT}")
endif()
set(LAUNCHER_CACERT_S "${CMAKE_BINARY_DIR}/cacert.S")
execute_process(
    COMMAND "${NX_BIN2S}" "${LAUNCHER_CACERT}"
    OUTPUT_FILE "${LAUNCHER_CACERT_S}"
    RESULT_VARIABLE BIN2S_RESULT)
if(NOT BIN2S_RESULT EQUAL 0)
    message(FATAL_ERROR "bin2s failed")
endif()

add_executable(launcher
    "${CMAKE_BINARY_DIR}/nsmbw_logo.S"
    "${LAUNCHER_CACERT_S}"
    source/main.cpp
    source/app.cpp
    source/log.cpp
    source/assets.cpp
    source/basedata.cpp
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
    source/ui/menus/menu_download.cpp
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

target_compile_options(launcher PRIVATE -Wall -Wextra)

target_link_libraries(launcher PRIVATE
    nanovg
    yyjson
    nsmbw_disc
    PkgConfig::CURL
    PkgConfig::ZSTD
    ${BZ2_LIBRARY}
    deko3d
    nx
)

find_program(NX_NACPTOOL NAMES nacptool HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)
find_program(NX_ELF2NRO NAMES elf2nro HINTS "$ENV{DEVKITPRO}/tools/bin" REQUIRED)

set(NACP "${CMAKE_BINARY_DIR}/launcher.nacp")
execute_process(
    COMMAND "${NX_NACPTOOL}" --create "NSMBW Launcher" "WiiCompiled" "${PROJECT_VERSION}" "${NACP}"
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
