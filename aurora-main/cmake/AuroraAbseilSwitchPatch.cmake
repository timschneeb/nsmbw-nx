# Source-level fix for the standalone abseil-cpp fetch on Nintendo Switch.
#
# Same contract as AuroraSDL3Patches.cmake: an exact string replacement, checked
# and idempotent. Run as `cmake -DABSEIL_SOURCE_DIR=<dir> -P
# AuroraAbseilSwitchPatch.cmake` from the FetchContent PATCH_COMMAND in
# extern/CMakeLists.txt (Switch only).
#
# newlib's pthread_t is a pointer, so the fallback GetTID's
# static_cast<pid_t>(pthread_self()) does not compile. Route through uintptr_t
# (same fix as the Dusklight/Melee dawn-deps-abseil patches).

function(_aurora_abseil_replace file description old new)
  file(READ "${file}" _content)
  string(FIND "${_content}" "${new}" _already)
  if (NOT _already EQUAL -1)
    return()
  endif ()
  string(FIND "${_content}" "${old}" _anchor)
  if (_anchor EQUAL -1)
    message(FATAL_ERROR "aurora: abseil patch '${description}' does not apply to ${file}; "
      "the vendored abseil version changed, review AuroraAbseilSwitchPatch.cmake")
  endif ()
  string(REPLACE "${old}" "${new}" _content "${_content}")
  file(WRITE "${file}" "${_content}")
  message(STATUS "aurora: abseil patch applied: ${description}")
endfunction()

if (NOT DEFINED ABSEIL_SOURCE_DIR)
  message(FATAL_ERROR "aurora: ABSEIL_SOURCE_DIR is required")
endif ()
set(_sysinfo "${ABSEIL_SOURCE_DIR}/absl/base/internal/sysinfo.cc")
if (NOT EXISTS "${_sysinfo}")
  message(FATAL_ERROR "aurora: abseil source tree at ${ABSEIL_SOURCE_DIR} has no absl/base/internal/sysinfo.cc")
endif ()

_aurora_abseil_replace("${_sysinfo}" "GetTID via uintptr_t for pointer pthread_t"
[==[  return static_cast<pid_t>(pthread_self());]==]
[==[  return static_cast<pid_t>(reinterpret_cast<uintptr_t>(pthread_self()));]==])

# newlib has no <link.h>, so the ELF memory image (stack-trace symbolization,
# diagnostics-only) must stay disabled. Same fix as the Dusklight/Melee
# dawn-deps-abseil patches.
set(_elf_image "${ABSEIL_SOURCE_DIR}/absl/debugging/internal/elf_mem_image.h")
_aurora_abseil_replace("${_elf_image}" "no ABSL_HAVE_ELF_MEM_IMAGE on Switch"
[==[    !defined(__VXWORKS__) && !defined(__hexagon__)]==]
[==[    !defined(__VXWORKS__) && !defined(__hexagon__) && !defined(__SWITCH__)]==])

# newlib's struct tm has no tm_gmtoff/tm_zone fields. Stub both (Horizon runs
# UTC for this purpose). Same fix as the Dusklight/Melee dawn-deps-abseil
# patches.
set(_tz_libc "${ABSEIL_SOURCE_DIR}/absl/time/internal/cctz/src/time_zone_libc.cc")
_aurora_abseil_replace("${_tz_libc}" "tm_gmtoff/tm_zone stubs for newlib"
[==[#else
// Adapt to different spellings of the struct std::tm extension fields.]==]
[==[#elif defined(__SWITCH__)
auto tm_gmtoff(const std::tm&) -> long { return 0; }
auto tm_zone(const std::tm&) -> const char* { return "UTC"; }
#else
// Adapt to different spellings of the struct std::tm extension fields.]==])
