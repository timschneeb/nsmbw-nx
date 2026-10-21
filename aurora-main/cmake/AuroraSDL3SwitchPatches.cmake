# Switch SDL3 patches. Runs the base patches, then the Switch port patch.
#
# sdl3-switch.patch is taken from the Dusklight Switch port
# (platforms/switch/patches/sdl3-switch.patch) and applies cleanly to the
# SDL version pinned by AURORA_SDL3_VERSION. It adds libnx video, audio and
# joystick backends plus the CMake platform branch.
#
# Script mode: cmake -DSDL_SOURCE_DIR=<dir> -P AuroraSDL3SwitchPatches.cmake

include("${CMAKE_CURRENT_LIST_DIR}/AuroraSDL3Patches.cmake")

function(aurora_sdl3_apply_switch_patches sdl_source_dir)
  aurora_sdl3_apply_patches("${sdl_source_dir}")

  set(_stamp "${sdl_source_dir}/.aurora-sdl3-switch-patch-stamp")
  if(EXISTS "${_stamp}")
    return()
  endif()

  find_program(_patch_exe NAMES patch)
  if(NOT _patch_exe)
    message(FATAL_ERROR "aurora: GNU patch is required to patch SDL3 for Switch")
  endif()

  execute_process(
    COMMAND "${_patch_exe}" -p1 --no-backup-if-mismatch
    INPUT_FILE "${CMAKE_CURRENT_LIST_DIR}/switch-patches/sdl3-switch.patch"
    WORKING_DIRECTORY "${sdl_source_dir}"
    RESULT_VARIABLE _patch_result
    OUTPUT_VARIABLE _patch_out
    ERROR_VARIABLE _patch_err)
  if(NOT _patch_result EQUAL 0)
    message(FATAL_ERROR "aurora: SDL3 Switch patch failed:\n${_patch_out}\n${_patch_err}")
  endif()
  file(WRITE "${_stamp}" "sdl3-switch.patch applied\n")
  message(STATUS "aurora: SDL3 patch applied: Switch libnx backends")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE AND DEFINED SDL_SOURCE_DIR)
  aurora_sdl3_apply_switch_patches("${SDL_SOURCE_DIR}")
endif()
