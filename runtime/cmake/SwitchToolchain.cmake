# Nintendo Switch homebrew toolchain (devkitPro devkitA64 + libnx).
# Usage:
#   cmake -S runtime -B build-switch -G Ninja \
#     --toolchain runtime/cmake/SwitchToolchain.cmake \
#     -DCMAKE_BUILD_TYPE=Release -DMKW_BUILD_PRODUCTS=OFF
#
# Modeled on the Dusklight Switch port's DevkitA64Libnx.cmake.

cmake_minimum_required(VERSION 3.25)

set(CMAKE_SYSTEM_NAME NintendoSwitch)
set(CMAKE_SYSTEM_VERSION "DKA-NX-19")
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not set.")
endif()

list(APPEND CMAKE_MODULE_PATH "$ENV{DEVKITPRO}/cmake")

set(DEVKITPRO "$ENV{DEVKITPRO}")

set(NX_ROOT "${DEVKITPRO}/libnx")

# SDL3-switch convention.
set(SWITCH TRUE)
if(NOT EXISTS "${NX_ROOT}/switch.specs")
    message(WARNING "SwitchToolchain: NX_ROOT switch.specs not found at ${NX_ROOT}/switch.specs")
endif()

set(MKW_DEVKITA64 "${DEVKITPRO}/devkitA64")
set(MKW_LIBNX "${DEVKITPRO}/libnx")
set(MKW_SWITCH_PORTLIBS "${DEVKITPRO}/portlibs/switch")

find_program(CMAKE_C_COMPILER NAMES aarch64-none-elf-gcc HINTS "${MKW_DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_CXX_COMPILER NAMES aarch64-none-elf-g++ HINTS "${MKW_DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_ASM_COMPILER NAMES aarch64-none-elf-gcc HINTS "${MKW_DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_AR NAMES aarch64-none-elf-gcc-ar HINTS "${MKW_DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)
find_program(CMAKE_RANLIB NAMES aarch64-none-elf-gcc-ranlib HINTS "${MKW_DEVKITA64}/bin" NO_DEFAULT_PATH REQUIRED)

list(APPEND CMAKE_PROGRAM_PATH "${DEVKITPRO}/tools/bin" "${MKW_DEVKITA64}/bin")

set(CMAKE_FIND_ROOT_PATH "${DEVKITPRO}" "${MKW_DEVKITA64}" "${MKW_SWITCH_PORTLIBS}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_INSTALL_PREFIX "${MKW_SWITCH_PORTLIBS}" CACHE PATH "Install prefix")
set(CMAKE_PREFIX_PATH "${MKW_SWITCH_PORTLIBS}" CACHE PATH "Find prefix")

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)
# No libdl on Horizon.
set(CMAKE_DL_LIBS "" CACHE STRING "No libdl on Switch" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Tegra X1 (Cortex-A57) flags. Matches the Dusklight Switch port.
set(MKW_SWITCH_ARCH_FLAGS
    "-march=armv8-a+crc+crypto;-mtune=cortex-a57;-mtp=soft;-fPIE"
    CACHE STRING "Tegra X1 arch flags")

add_compile_options(${MKW_SWITCH_ARCH_FLAGS})
# Keep x29 a frame pointer so the fatal-log host stack walk can unwind.
add_compile_options("-fno-omit-frame-pointer")
# x28 holds the guest flat window base, so guest memory is one register-relative access.
add_compile_options("-ffixed-x28")
add_compile_options("-I${MKW_LIBNX}/include")
add_link_options(${MKW_SWITCH_ARCH_FLAGS})
add_link_options("-L${MKW_LIBNX}/lib")
# MKW_PLATFORM_SWITCH gates Switch-only shims and ifdefs out unsupported
# features (updater, Discord, Tailscale, SDL dialogs). PC paths are untouched.
add_compile_definitions(SWITCH __SWITCH__=1 MKW_PLATFORM_SWITCH=1 TARGET_SWITCH=1 _DEFAULT_SOURCE)
# Guest MMIO loads fault into __libnx_exception_entry (fastmem_entry.S/fastmem_fault.cpp)
# instead of taking the inline FlatReadNeedsPolicy test. Remove this define to revert.
add_compile_definitions(MKW_SWITCH_FASTMEM_MMIO=1)

set(MKW_SWITCH_SPECS_FLAG "-specs=${MKW_LIBNX}/switch.specs" CACHE STRING "libnx specs flag")

# Dawn vendors protobuf and needs a host protoc when cross-compiling.
find_program(MKW_SWITCH_HOST_PROTOC NAMES protoc)
if(MKW_SWITCH_HOST_PROTOC)
    set(PROTOC_EXECUTABLE "${MKW_SWITCH_HOST_PROTOC}" CACHE FILEPATH "Host protoc for Dawn")
endif()
