# Switch-only patch for Tracy's TracySystem.cpp (applied via PATCH_COMMAND on
# the freshly extracted tree, so vendor aurora-main stays the single source of
# truth). Horizon has no kernel TID to return and no getlogin_r, mirroring the
# upstream dusklight-nx tracy-switch.patch hunks.
if(NOT DEFINED TRACY_SOURCE_DIR)
  message(FATAL_ERROR "AuroraTracySwitchPatch.cmake requires -DTRACY_SOURCE_DIR=<tracy source dir>")
endif()
set(_tracy_system "${TRACY_SOURCE_DIR}/public/common/TracySystem.cpp")
if(NOT EXISTS "${_tracy_system}")
  message(FATAL_ERROR "Tracy source not found at ${_tracy_system}")
endif()

function(_tracy_switch_replace anchor replacement description)
  file(READ "${_tracy_system}" _contents)
  string(FIND "${_contents}" "${replacement}" _already)
  if(NOT _already EQUAL -1)
    message(STATUS "aurora: Tracy Switch patch already applied (${description})")
    return()
  endif()
  string(FIND "${_contents}" "${anchor}" _pos)
  if(_pos EQUAL -1)
    message(FATAL_ERROR "aurora: Tracy Switch patch anchor missing (${description}); the Tracy revision may have drifted, update AuroraTracySwitchPatch.cmake")
  endif()
  string(REPLACE "${anchor}" "${replacement}" _contents "${_contents}")
  file(WRITE "${_tracy_system}" "${_contents}")
  message(STATUS "aurora: applied Tracy Switch patch (${description})")
endfunction()

_tracy_switch_replace(
  "#elif defined __EMSCRIPTEN__\n    // Not supported, but let it compile.\n    return 0;\n#else"
  "#elif defined __EMSCRIPTEN__\n    // Not supported, but let it compile.\n    return 0;\n#elif defined __SWITCH__\n    // Not supported, but let it compile.\n    return 0;\n#else"
  "thread-handle")

_tracy_switch_replace(
  "#elif defined __ANDROID__\n    const auto user = getlogin();\n    if( user ) return user;\n    return \"(?)\";\n#else"
  "#elif defined __ANDROID__\n    const auto user = getlogin();\n    if( user ) return user;\n    return \"(?)\";\n#elif defined __SWITCH__\n    return \"switch\";\n#else"
  "user-login")
