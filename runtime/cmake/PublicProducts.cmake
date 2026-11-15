# Public WiiCompiled product graph.
#
# The translator owns the translated build graph. Mario Kart's profile-neutral
# functions are compiled once into mkw_base_shared; only callers whose direct
# ABI differs between profiles receive small base/RR variants.

set(DATA_INIT_FILE "${MKW_GENERATED_DIR}/data_sections_init.cpp")
set(DATA_INIT_BLOB_ASM "${MKW_GENERATED_DIR}/data_sections_init_blobs.S")
if(EXISTS "${DATA_INIT_FILE}")
    list(APPEND SOURCES "${DATA_INIT_FILE}")
endif()
# Crash-report symbolization table emitted by generate-data-init. The stub
# (deliberately outside the globbed src/ tree so it is never picked up twice)
# keeps link succeeding when the generated table has not been produced yet.
set(GUEST_SYMBOL_TABLE_FILE "${MKW_GENERATED_DIR}/guest_symbol_table.cpp")
if(EXISTS "${GUEST_SYMBOL_TABLE_FILE}")
    list(APPEND SOURCES "${GUEST_SYMBOL_TABLE_FILE}")
else()
    list(APPEND SOURCES "${MKW_RUNTIME_SOURCE_DIR}/cmake/guest_symbol_table_stub.cpp")
endif()
if(EXISTS "${DATA_INIT_BLOB_ASM}")
    enable_language(ASM)
    set_source_files_properties("${DATA_INIT_BLOB_ASM}" PROPERTIES LANGUAGE ASM SKIP_UNITY_BUILD_INCLUSION ON)
    list(APPEND SOURCES "${DATA_INIT_BLOB_ASM}")
endif()
list(REMOVE_DUPLICATES SOURCES)

if(MKW_PLATFORM_MACOS)
    find_library(MKW_IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(MKW_COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
    find_library(MKW_COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
endif()

# Two-pass PGO for the Switch build. Only the runtime and the translated shards are instrumented;
# aurora and the vendored libraries are not on the guest's instruction stream, and instrumenting
# Dawn would triple the recording build for nothing.
set(MKW_PGO_COMPILE_OPTIONS "")
set(MKW_PGO_LINK_OPTIONS "")
if(MKW_PGO STREQUAL "generate")
    # -ftest-coverage: devkitA64's GCC 16 writes no notes file for -fprofile-generate alone, and
    # the use pass needs it. Single, non-atomic counters because the A57 has no LSE and an atomic
    # bump is an exclusive-monitor retry loop. -fno-profile-values: libgcov's value profiler reads
    # TLS through TPIDR_EL0, which Horizon resets, so it crashes during static init.
    set(MKW_PGO_COMPILE_OPTIONS -fprofile-generate -ftest-coverage -fprofile-update=single -fno-profile-values)
    set(MKW_PGO_LINK_OPTIONS -fprofile-generate -fprofile-update=single -fno-profile-values)
elseif(MKW_PGO STREQUAL "use")
    # -fprofile-use turns on O3-class loop transforms; code growth is a loss on this
    # i-cache-bound corpus, so keep them off and let the profile drive layout and branches.
    # No -freorder-blocks-and-partition: devkitA64's GCC emits jump-table entries that subtract
    # labels across hot and .text.unlikely sections, which the assembler rejects.
    set(MKW_PGO_COMPILE_OPTIONS
        -fprofile-use -fprofile-correction -Wno-missing-profile -fno-profile-values
        -Wno-error=coverage-mismatch -fno-reorder-functions
        -fno-unroll-loops -fno-peel-loops -fno-unswitch-loops -fno-split-loops
        -fno-tree-loop-distribution -fno-loop-interchange -fno-loop-unroll-and-jam
        -fno-predictive-commoning -fno-version-loops-for-strides -fno-ipa-cp-clone
        -fno-tracer -fvect-cost-model=very-cheap)
elseif(NOT MKW_PGO STREQUAL "")
    message(FATAL_ERROR "MKW_PGO must be empty, generate or use")
endif()

function(mkw_apply_common_compile_options target)
    target_compile_options(${target} PRIVATE -O3 -ffast-math -w -pipe ${MKW_PGO_COMPILE_OPTIONS})
endfunction()

function(mkw_apply_translated_compile_options target)
    target_compile_options(${target} PRIVATE
        -O2 ${MKW_TRANSLATED_PPC_FP_OPTIONS}
        $<$<BOOL:${MKW_ENABLE_SHADOW_SAMPLER}>:-finstrument-functions>
        $<$<CXX_COMPILER_ID:Clang>:-fno-slp-vectorize>
        $<$<CXX_COMPILER_ID:GNU>:-fno-tree-slp-vectorize>
        # The shards are built -fPIC, so GCC treats every non-static function as interposable and
        # refuses to inline or specialise calls to it. Nothing is interposed in the NRO, and a
        # shard is a contiguous guest address range, so the hot call chains sit inside one TU.
        $<$<CXX_COMPILER_ID:GNU>:-fno-semantic-interposition>
        ${MKW_PGO_COMPILE_OPTIONS}
        -w -pipe)
endfunction()

function(mkw_configure_object_target target)
    target_include_directories(${target} PRIVATE
        "${MKW_RUNTIME_SOURCE_DIR}/include"
        "${MKW_RUNTIME_SOURCE_DIR}/src"
        "${MKW_GENERATED_DIR}"
        "${MKW_RUNTIME_SOURCE_DIR}/../aurora-main/include")
    target_compile_definitions(${target} PRIVATE
        TARGET_PC)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
endfunction()

# Translated shard TUs are the memory-hungry compiles; everything else in the build is
# comparatively small. A dedicated Ninja job pool caps how many of them run at once so the
# global parallelism can use every core for the cheap TUs without the memory ceiling being a
# guess. The pool depth is per-machine (derived from installed RAM by LocalBuild.ps1) and is
# deliberately not part of the canonical flag set: it changes scheduling, never output bytes.
if(MKW_TRANSLATED_COMPILE_JOBS GREATER 0)
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS "mkw_translated=${MKW_TRANSLATED_COMPILE_JOBS}")
endif()

function(mkw_bound_translated_compiles target)
    if(MKW_TRANSLATED_COMPILE_JOBS GREATER 0)
        set_property(TARGET ${target} PROPERTY JOB_POOL_COMPILE mkw_translated)
    endif()
endfunction()

function(mkw_configure_translated_target target)
    mkw_configure_object_target(${target})
    mkw_apply_translated_compile_options(${target})
    mkw_bound_translated_compiles(${target})
endfunction()

# Codecs the disc image reader needs for .wia/.rvz groups. Zstandard is the copy aurora already
# builds; bzip2 comes from the platform (absent on a platform that has none: those images are then
# refused with a sentence instead of being misread).
add_library(mkw_disc_codecs INTERFACE)
if(TARGET libzstd_static)
    target_link_libraries(mkw_disc_codecs INTERFACE libzstd_static)
elseif(TARGET zstd::libzstd_static)
    target_link_libraries(mkw_disc_codecs INTERFACE zstd::libzstd_static)
endif()
find_package(BZip2 QUIET)
if(BZip2_FOUND)
    target_link_libraries(mkw_disc_codecs INTERFACE BZip2::BZip2)
endif()

add_library(mkw_runtime_common OBJECT ${SOURCES})
mkw_configure_object_target(mkw_runtime_common)
target_compile_features(mkw_runtime_common PRIVATE cxx_std_20)
target_compile_definitions(mkw_runtime_common PRIVATE
    SDL_MAIN_HANDLED
    _DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION)
if(MKW_ENABLE_SHADOW_SAMPLER)
    target_compile_definitions(mkw_runtime_common PRIVATE MKW_SHADOW_SAMPLER=1)
endif()
target_link_libraries(mkw_runtime_common PRIVATE
    aurora::gx aurora::pad aurora::si aurora::vi aurora::mtx)
if(MKW_PLATFORM_SWITCH AND TARGET Freetype::Freetype)
    # The boot splash draws the shared system font with FreeType.
    target_link_libraries(mkw_runtime_common PRIVATE Freetype::Freetype)
endif()
if(MKW_PLATFORM_SWITCH)
    # switch-curl: the HD texture pack download (src/hd_texture_pack.cpp) runs in-process,
    # because Horizon has no curl or wget on the PATH.
    target_include_directories(mkw_runtime_common SYSTEM PRIVATE "${MKW_SWITCH_PORTLIBS}/include")
endif()
target_link_libraries(mkw_runtime_common PRIVATE mkw_platform mkw::pugixml mkw::lzma mkw::toml11 mkw::cryptopp
    mkw::mbedtls mkw_disc_codecs)
nsmbw_bt_passthrough_configure_runtime(mkw_runtime_common)
if(MKW_PLATFORM_WINDOWS)
    # ole32/uuid/comctl32: the first-start game picker (game_picker_win32.cpp).
    target_link_libraries(mkw_runtime_common PRIVATE shell32 windowsapp ole32 uuid comctl32)
elseif(MKW_PLATFORM_LINUX)
    # ${CMAKE_DL_LIBS} for music_attenuation.cpp's dlopen of libdbus-1 (MPRIS
    # media monitoring).
    target_link_libraries(mkw_runtime_common PRIVATE mkw::libco ${CMAKE_DL_LIBS})
elseif(MKW_PLATFORM_ANDROID)
    target_link_libraries(mkw_runtime_common PRIVATE android log ${CMAKE_DL_LIBS})
    target_include_directories(mkw_runtime_common PRIVATE
        "${MKW_RUNTIME_SOURCE_DIR}/third_party/libadrenotools/include")
endif()

if(MKW_PLATFORM_MACOS)
    # CoreAudio framework is required for automatic music muting on macOS.
    target_link_libraries(mkw_runtime_common PRIVATE "${MKW_COREAUDIO_FRAMEWORK}")
    if(MKW_PLATFORM_MACOS_X86_64)
        # libco is used by Intel macOS. Apple Silicon uses the local
        # x18-safe assembly backend and therefore does not define mkw::libco.
        target_link_libraries(mkw_runtime_common PRIVATE mkw::libco)
    endif()
endif()

if(MKW_CPPWINRT_INCLUDE_DIR)
    if(NOT EXISTS "${MKW_CPPWINRT_INCLUDE_DIR}/winrt/base.h")
        message(FATAL_ERROR
            "MKW_CPPWINRT_INCLUDE_DIR does not contain winrt/base.h: ${MKW_CPPWINRT_INCLUDE_DIR}")
    endif()
    target_include_directories(mkw_runtime_common PRIVATE "${MKW_CPPWINRT_INCLUDE_DIR}")
endif()

# Keep runtime unity units small and semantically related. The old generated-TU
# batch size put all 57 native runtime sources into one memory-heavy compiler job.
foreach(source IN LISTS SOURCES)
    get_filename_component(source_name "${source}" NAME_WE)
    string(REPLACE "\\" "/" source_normalized "${source}")
    if(source_normalized MATCHES "/hle/gx/")
        set(runtime_group "gx_bridge")
    elseif(source_name MATCHES "network|socket|dns|dwc|ios")
        set(runtime_group "network_ios")
    elseif(source_name MATCHES "os_|system|memory|fiber|scheduler")
        set(runtime_group "guest_system")
    elseif(source_name MATCHES "debug|trace|prof")
        set(runtime_group "diagnostics")
    else()
        string(SHA256 source_hash "${source_name}")
        string(SUBSTRING "${source_hash}" 0 4 source_hash_prefix)
        math(EXPR runtime_bucket "0x${source_hash_prefix} % 8")
        set(runtime_group "runtime_${runtime_bucket}")
    endif()
    set_source_files_properties("${source}" PROPERTIES UNITY_GROUP "${runtime_group}")
endforeach()
# PPC semantic sources are excluded from unity/PCH and configured in CMakeLists.txt.
set_target_properties(mkw_runtime_common PROPERTIES UNITY_BUILD ON UNITY_BUILD_MODE GROUP)
target_precompile_headers(mkw_runtime_common PRIVATE "${MKW_RUNTIME_SOURCE_DIR}/include/mkw_pch.h")
mkw_apply_common_compile_options(mkw_runtime_common)

# Host ISA guard. Windows and Linux x86_64 product targets use x86-64-v3, so
# this object deliberately keeps the plain baseline ISA and checks the CPU
# before any AVX2/FMA code can execute. AArch64 has no equivalent optional ISA
# floor to probe: NEON/FMA are architectural requirements.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
    add_library(mkw_cpu_baseline OBJECT "${MKW_CPU_BASELINE_SOURCE}")
    target_compile_features(mkw_cpu_baseline PRIVATE cxx_std_17)
    set_target_properties(mkw_cpu_baseline PROPERTIES UNITY_BUILD OFF)
    target_compile_options(mkw_cpu_baseline PRIVATE -w)
endif()

if(NOT MKW_BASE_COMMON_SHARDS)
    message(FATAL_ERROR "Translator build graph contains no shared base shards")
endif()

add_library(mkw_base_shared STATIC ${MKW_BASE_COMMON_SHARDS})
mkw_configure_translated_target(mkw_base_shared)
target_precompile_headers(mkw_base_shared PRIVATE "${MKW_RUNTIME_SOURCE_DIR}/include/mkw_pch.h")

if(MKW_BASE_PORTABLE_SENSITIVE_SHARDS)
    add_library(mkw_base_sensitive OBJECT ${MKW_BASE_PORTABLE_SENSITIVE_SHARDS})
    mkw_configure_translated_target(mkw_base_sensitive)
    target_precompile_headers(mkw_base_sensitive REUSE_FROM mkw_base_shared)
endif()

if(MKW_HAVE_RETRO_REWIND)
    if(MKW_RETRO_PORTABLE_SENSITIVE_SHARDS)
        add_library(mkw_retro_sensitive OBJECT ${MKW_RETRO_PORTABLE_SENSITIVE_SHARDS})
        mkw_configure_translated_target(mkw_retro_sensitive)
        target_precompile_headers(mkw_retro_sensitive REUSE_FROM mkw_base_shared)
    endif()

    set(MKW_RETRO_TRANSLATED_SOURCES ${MKW_RETRO_MOD_SHARDS} ${MKW_RETRO_EXTRA_SOURCES})
    set(MKW_RETRO_BLOB_OBJECTS)
    foreach(source IN LISTS MKW_RETRO_EXTRA_SOURCES)
        if(source MATCHES "\\.S$")
            enable_language(ASM)
            set_source_files_properties("${source}" PROPERTIES LANGUAGE ASM SKIP_PRECOMPILE_HEADERS ON)
        endif()
    endforeach()
    add_library(mkw_retro_rewind_functions OBJECT ${MKW_RETRO_TRANSLATED_SOURCES})
    mkw_configure_translated_target(mkw_retro_rewind_functions)
    target_precompile_headers(mkw_retro_rewind_functions REUSE_FROM mkw_base_shared)
endif()

function(mkw_configure_product target)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:mkw_runtime_common>)
    # Startup CPU check. Must stay a separate object library so it keeps the
    # plain baseline ISA while everything around it is built for x86-64-v3.
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
        target_sources(${target} PRIVATE $<TARGET_OBJECTS:mkw_cpu_baseline>)
    endif()
    target_include_directories(${target} PRIVATE
        "${MKW_RUNTIME_SOURCE_DIR}/include"
        "${MKW_RUNTIME_SOURCE_DIR}/src"
        "${MKW_GENERATED_DIR}"
        "${MKW_RUNTIME_SOURCE_DIR}/../aurora-main/include")
    target_compile_definitions(${target} PRIVATE
        SDL_MAIN_HANDLED _DISABLE_STRING_ANNOTATION _DISABLE_VECTOR_ANNOTATION TARGET_PC)
    target_compile_features(${target} PRIVATE cxx_std_20)
    mkw_apply_common_compile_options(${target})
    if(MKW_PGO_LINK_OPTIONS)
        target_link_options(${target} PRIVATE ${MKW_PGO_LINK_OPTIONS})
    endif()
    if(MKW_PGO STREQUAL "generate")
        target_compile_definitions(mkw_runtime_common PRIVATE MKW_PGO_GENERATE=1)
    endif()
    # The dispatch-table and registration shards compile inside the product target itself and
    # include the same fat translated headers; bound them by the same pool.
    mkw_bound_translated_compiles(${target})
    target_link_libraries(${target} PRIVATE
        mkw_platform mkw_base_shared mkw::pugixml mkw::lzma mkw::toml11 mkw::cryptopp mkw::mbedtls
        mkw_disc_codecs)

    target_link_libraries(${target} PRIVATE
        aurora::gx aurora::pad aurora::si aurora::vi aurora::mtx)
    nsmbw_bt_passthrough_link_product(${target})
    if(MKW_PLATFORM_MACOS)
        target_link_libraries(${target} PRIVATE
            "${MKW_IOKIT_FRAMEWORK}" "${MKW_COREFOUNDATION_FRAMEWORK}" "${MKW_COREAUDIO_FRAMEWORK}")
        target_link_options(${target} PRIVATE
            "LINKER:-U,_OBJC_CLASS_$_MTLLogStateDescriptor")
    endif()
    if(EXISTS "${MKW_AURORA_DIR}/cmake/AuroraCopyRuntimeDLLs.cmake")
        include("${MKW_AURORA_DIR}/cmake/AuroraCopyRuntimeDLLs.cmake")
        aurora_copy_runtime_dlls(${target})
    endif()
    if(TARGET sqlite3)
        get_target_property(MKW_SQLITE_TARGET_TYPE sqlite3 TYPE)
    endif()
    if(TARGET sqlite3 AND
       (MKW_SQLITE_TARGET_TYPE STREQUAL "SHARED_LIBRARY" OR
        MKW_SQLITE_TARGET_TYPE STREQUAL "MODULE_LIBRARY"))
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:sqlite3> $<TARGET_FILE_DIR:${target}>)
    endif()

    if(MKW_PLATFORM_WINDOWS)
        target_link_libraries(${target} PRIVATE
            dbghelp user32 winmm ws2_32 iphlpapi secur32 crypt32 windowsapp winhttp comctl32)

        set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
    elseif(MKW_PLATFORM_LINUX OR MKW_PLATFORM_MACOS_X86_64)
        # mkw_runtime_common is an OBJECT library: WiiCompiled/RetroRewind only pull in its .o
        # files via $<TARGET_OBJECTS:>, which does not propagate mkw_runtime_common's own
        # target_link_libraries (object libraries don't carry usage requirements to a consumer
        # that isn't itself linked against as a target). fiber_manager.cpp's co_* calls live in
        # those objects, so the actual executable link needs mkw::libco directly, same as it
        # needs it independently of that first `if(WIN32)` branch above. ${CMAKE_DL_LIBS} is
        # here for the same reason: music_attenuation.cpp's dlopen(libdbus-1) lives in those
        # objects (empty string on glibc >= 2.34, where dl* is in libc).
        target_link_libraries(${target} PRIVATE mkw::libco ${CMAKE_DL_LIBS})
    elseif(MKW_PLATFORM_ANDROID)
        # The same object-library gap as above: the product link needs the libraries the runtime
        # objects call into. SDL's Android activity loads the product as libmain.so and runs its
        # exported SDL_main (runtime/src/main.cpp). 16 KiB pages keep it loadable on Android 15+
        # devices that use them.
        target_link_libraries(${target} PRIVATE android log ${CMAKE_DL_LIBS})
        target_link_options(${target} PRIVATE "LINKER:-z,max-page-size=16384" "LINKER:--no-undefined")
        # Calls between the library's own functions bind inside it: as a shared library with default
        # visibility, every call from one translated function to another (and to the runtime) went
        # through a PLT stub and a GOT load, 5.8% of the game thread's samples on a slow Android device.
        # Nothing interposes on libmain's functions; the JNI entry points stay exported.
        target_link_options(${target} PRIVATE "LINKER:-Bsymbolic-functions")
        # A picked GPU driver: Dawn's dlopen of libvulkan.so lands in src/android/gpu_driver.cpp, which
        # opens libadrenotools.so only then. Built here, staged as assets by stage-android.ps1.
        target_link_options(${target} PRIVATE "LINKER:--wrap=dlopen" "LINKER:--wrap=dlclose")
        add_dependencies(${target} adrenotools main_hook hook_impl)
        set_target_properties(${target} PROPERTIES OUTPUT_NAME main)
    elseif(MKW_PLATFORM_SWITCH)
        # Product graphics: system Dawn (same ref, via aurora above) plus the
        # prebuilt Mesa NVK Vulkan driver (vk_icdGetInstanceProcAddr, vk_*).
        # (SDL's egl refs resolve to stubs in egl_stubs.cpp; harfbuzz+bz2 ride
        # on Freetype::Freetype itself, see runtime/CMakeLists.txt.)
        target_link_libraries(${target} PRIVATE mkw_switch_nvk)
        target_link_libraries(${target} PRIVATE "${MKW_SWITCH_PORTLIBS}/lib/libcurl.a")
        # libnx_shims.cpp: force 4 MiB stacks on attribute-less pthreads and map
        # thrd_create's newlib success code to 0 for Mesa.
        target_link_options(${target} PRIVATE
            "LINKER:--wrap=pthread_create"
            "LINKER:--wrap=thrd_create")
    endif()
    if(MKW_PLATFORM_WINDOWS)
        foreach(runtime_dll libc++.dll libunwind.dll)
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" "--print-file-name=${runtime_dll}"
                OUTPUT_VARIABLE runtime_dll_path
                OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(NOT EXISTS "${runtime_dll_path}")
                get_filename_component(mkw_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
                set(runtime_dll_path "${mkw_compiler_bin}/${runtime_dll}")
            endif()
            if(NOT EXISTS "${runtime_dll_path}")
                message(FATAL_ERROR "llvm-mingw runtime DLL not found: ${runtime_dll}")
            endif()
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${runtime_dll_path}" $<TARGET_FILE_DIR:${target}>)
        endforeach()
    endif()

    set(MKW_WII_BOOTSTRAP_SOURCE_DIR "${MKW_RUNTIME_SOURCE_DIR}/assets/wii")
    if(NOT EXISTS "${MKW_WII_BOOTSTRAP_SOURCE_DIR}/shared2/wc24")
        message(FATAL_ERROR "Missing Wii first-run bootstrap payload: ${MKW_WII_BOOTSTRAP_SOURCE_DIR}")
    endif()
    # On Switch the payload goes into the NRO's RomFS instead of a folder beside it.
    if(NOT MKW_PLATFORM_SWITCH)
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${MKW_WII_BOOTSTRAP_SOURCE_DIR}" "$<TARGET_FILE_DIR:${target}>/wii_bootstrap")
    endif()

    set(MKW_DSP_COEFFICIENT_ROM "${MKW_RUNTIME_SOURCE_DIR}/assets/dsp/dsp_coef.bin")
    if(NOT EXISTS "${MKW_DSP_COEFFICIENT_ROM}")
        message(FATAL_ERROR "Missing Wii DSP coefficient ROM: ${MKW_DSP_COEFFICIENT_ROM}")
    endif()
    file(SHA256 "${MKW_DSP_COEFFICIENT_ROM}" MKW_DSP_COEFFICIENT_ROM_SHA256)
    if(NOT MKW_DSP_COEFFICIENT_ROM_SHA256 STREQUAL
       "d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179")
        message(FATAL_ERROR "Wii DSP coefficient ROM hash mismatch: ${MKW_DSP_COEFFICIENT_ROM_SHA256}")
    endif()
    # On Switch the ROM goes into the NRO's RomFS instead of a file beside it.
    if(NOT MKW_PLATFORM_SWITCH)
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MKW_DSP_COEFFICIENT_ROM}" "$<TARGET_FILE_DIR:${target}>/dsp_coef.bin")
    endif()
    # Aurora imports this portable recipe database into each user's writable
    # pipeline cache. Keep the upstream filename so its default resourcesPath
    # lookup works without application-specific configuration.
    set(MKW_INITIAL_PIPELINE_CACHE
        "${MKW_RUNTIME_SOURCE_DIR}/assets/pipeline/initial_pipeline_cache.db")
    # Newer reuses most of the base renderer and ships its own captured pipeline cache.
    if(NSMBW_GAME_ID STREQUAL "SMNE03")
        set(MKW_INITIAL_PIPELINE_CACHE
            "${MKW_RUNTIME_SOURCE_DIR}/assets/pipeline/newer_pipeline_cache.db")
    endif()
    if(NOT EXISTS "${MKW_INITIAL_PIPELINE_CACHE}")
        message(FATAL_ERROR "Missing transferable Aurora pipeline cache: ${MKW_INITIAL_PIPELINE_CACHE}")
    endif()
    if(NOT MKW_PLATFORM_SWITCH)
        add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MKW_INITIAL_PIPELINE_CACHE}"
            "$<TARGET_FILE_DIR:${target}>/initial_pipeline_cache.db")
    endif()

    # Non-Windows TLS (runtime/src/hle/net/network_ssl.cpp's mbed TLS path) needs a trusted root
    # CA bundle to verify server certificates against - Windows gets this for free from the OS via
    # Schannel, mbed TLS does not ship one itself. Not SHA256-pinned like the DSP ROM above: unlike
    # a fixed hardware ROM, this bundle is expected to be refreshed periodically as CAs rotate.
    # Windows gets its trust store from Schannel, so only the platforms that actually build the
    # mbed TLS path need the bundle beside the executable.
    if(NOT MKW_PLATFORM_WINDOWS)
        set(MKW_CA_CERTIFICATE_BUNDLE "${MKW_RUNTIME_SOURCE_DIR}/assets/certs/cacert.pem")
        if(NOT EXISTS "${MKW_CA_CERTIFICATE_BUNDLE}")
            message(FATAL_ERROR "Missing TLS root CA bundle: ${MKW_CA_CERTIFICATE_BUNDLE}")
        endif()
        if(NOT MKW_PLATFORM_SWITCH)
            add_custom_command(TARGET ${target} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MKW_CA_CERTIFICATE_BUNDLE}" "$<TARGET_FILE_DIR:${target}>/cacert.pem")
        endif()
    endif()

    if(MKW_PLATFORM_SWITCH)
        # Read-only assets the NRO embeds as its RomFS section; libnx reads them out of the NRO file
        # on the SD card on demand, so this costs no memory. One tree per product so parallel builds
        # never rewrite each other's.
        set(MKW_SWITCH_ROMFS_DIR "${CMAKE_BINARY_DIR}/romfs/${target}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${MKW_SWITCH_ROMFS_DIR}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${MKW_SWITCH_ROMFS_DIR}/wii_bootstrap"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${MKW_WII_BOOTSTRAP_SOURCE_DIR}" "${MKW_SWITCH_ROMFS_DIR}/wii_bootstrap"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MKW_CA_CERTIFICATE_BUNDLE}" "${MKW_SWITCH_ROMFS_DIR}/cacert.pem"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MKW_DSP_COEFFICIENT_ROM}" "${MKW_SWITCH_ROMFS_DIR}/dsp_coef.bin"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MKW_INITIAL_PIPELINE_CACHE}" "${MKW_SWITCH_ROMFS_DIR}/initial_pipeline_cache.db"
            COMMENT "Staging the NRO RomFS"
            VERBATIM)
    endif()
endfunction()

if(MKW_PLATFORM_ANDROID)
    add_library(WiiCompiled SHARED "${MKW_BASE_PRODUCT_SOURCE}" ${MKW_BASE_REGISTRATION_SOURCES})
else()
    add_executable(WiiCompiled "${MKW_BASE_PRODUCT_SOURCE}" ${MKW_BASE_REGISTRATION_SOURCES})
endif()
mkw_configure_product(WiiCompiled)
target_precompile_headers(WiiCompiled PRIVATE
    "${MKW_RUNTIME_SOURCE_DIR}/include/mkw_pch.h")
if(MKW_PLATFORM_SWITCH)
    # Pack the NRO as part of the link: elf2nro must get the NACP and icon as ASET assets or the
    # forwarder's loader aborts before the runtime starts, and the RomFS has to be embedded for the
    # bundled assets to resolve.
    find_program(MKW_NACPTOOL nacptool HINTS "$ENV{DEVKITPRO}/tools/bin")
    find_program(MKW_ELF2NRO elf2nro HINTS "$ENV{DEVKITPRO}/tools/bin")
    if(NOT MKW_NACPTOOL OR NOT MKW_ELF2NRO)
        message(FATAL_ERROR "nacptool and elf2nro are required for the Switch NRO (devkitPro tools)")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/switch")
    set(MKW_SWITCH_NACP "${CMAKE_BINARY_DIR}/switch/NSMBW.nacp")
    # The launcher reads this version from the NRO when a game was copied manually.
    file(READ "${CMAKE_SOURCE_DIR}/../VERSION" NSMBW_RECOMP_VERSION)
    string(STRIP "${NSMBW_RECOMP_VERSION}" NSMBW_RECOMP_VERSION)
    if(NSMBW_RECOMP_VERSION STREQUAL "")
        message(FATAL_ERROR "VERSION is empty")
    endif()
    set(MKW_SWITCH_NACP_VERSION "${NSMBW_RECOMP_VERSION}")
    execute_process(
        COMMAND "${MKW_NACPTOOL}" --create "New Super Mario Bros. Wii" "Aurora Recomp"
            "${MKW_SWITCH_NACP_VERSION}" "${MKW_SWITCH_NACP}"
        RESULT_VARIABLE MKW_NACP_RESULT)
    if(NOT MKW_NACP_RESULT EQUAL 0)
        message(FATAL_ERROR "nacptool failed to write ${MKW_SWITCH_NACP}")
    endif()
    add_custom_command(TARGET WiiCompiled POST_BUILD
        COMMAND "${MKW_ELF2NRO}" "$<TARGET_FILE:WiiCompiled>"
            "${CMAKE_BINARY_DIR}/NSMBW.nro"
            "--nacp=${MKW_SWITCH_NACP}"
            "--icon=${MKW_RUNTIME_SOURCE_DIR}/assets/icon/switch.jpg"
            "--romfsdir=${CMAKE_BINARY_DIR}/romfs/WiiCompiled"
        COMMENT "Packing NSMBW.nro"
        VERBATIM)

    # Release assets: the NRO in its SD card layout, the launcher and the update catalog,
    # plus the matching debug symbols for crash logs.
    if(NOT CMAKE_OBJCOPY)
        message(FATAL_ERROR "CMAKE_OBJCOPY is required for the Switch symbols archive")
    endif()
    set(MKW_SWITCH_DIST_DIR "${CMAKE_BINARY_DIR}/dist")
    set(MKW_SWITCH_NRO_STAGE "${CMAKE_BINARY_DIR}/package-nro")
    set(MKW_SWITCH_SYMBOLS_STAGE "${CMAKE_BINARY_DIR}/package-symbols")
    set(MKW_SWITCH_LAUNCHER_NRO "${CMAKE_SOURCE_DIR}/../build-launcher/launcher.nro"
        CACHE FILEPATH "Launcher NRO bundled into NSMBW-switch.zip")
    set(MKW_SWITCH_ASSETS_JSON "${CMAKE_SOURCE_DIR}/../launcher/assets.json"
        CACHE FILEPATH "assets.json bundled into NSMBW-switch.zip")
    set(MKW_SWITCH_BANNER "${CMAKE_SOURCE_DIR}/../launcher/assets/banners/newersmbw.png"
        CACHE FILEPATH "Mod banner bundled into NSMBW-switch.zip")

    set(MKW_SWITCH_PACKAGE_EXTRA)
    if(EXISTS "${MKW_SWITCH_LAUNCHER_NRO}")
        list(APPEND MKW_SWITCH_PACKAGE_EXTRA
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MKW_SWITCH_LAUNCHER_NRO}" "${MKW_SWITCH_NRO_STAGE}/switch/nsmbw/launcher.nro")
    else()
        message(WARNING "launcher.nro not found at ${MKW_SWITCH_LAUNCHER_NRO}; the package will not include it")
    endif()
    if(EXISTS "${MKW_SWITCH_ASSETS_JSON}")
        list(APPEND MKW_SWITCH_PACKAGE_EXTRA
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MKW_SWITCH_ASSETS_JSON}" "${MKW_SWITCH_NRO_STAGE}/switch/nsmbw/assets.json")
    else()
        message(WARNING "assets.json not found at ${MKW_SWITCH_ASSETS_JSON}; the package will not include it")
    endif()
    if(EXISTS "${MKW_SWITCH_BANNER}")
        list(APPEND MKW_SWITCH_PACKAGE_EXTRA
            COMMAND ${CMAKE_COMMAND} -E make_directory "${MKW_SWITCH_NRO_STAGE}/switch/nsmbw/banners"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${MKW_SWITCH_BANNER}" "${MKW_SWITCH_NRO_STAGE}/switch/nsmbw/banners/newersmbw.png")
    endif()

    add_custom_target(switch-package
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${MKW_SWITCH_NRO_STAGE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${MKW_SWITCH_NRO_STAGE}/switch/nsmbw/games/vanilla"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/NSMBW.nro"
            "${MKW_SWITCH_NRO_STAGE}/switch/nsmbw/games/vanilla/game.nro"
        ${MKW_SWITCH_PACKAGE_EXTRA}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MKW_SWITCH_DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${MKW_SWITCH_NRO_STAGE}"
            ${CMAKE_COMMAND} -E tar cf "${MKW_SWITCH_DIST_DIR}/NSMBW-switch.zip"
            --format=zip switch/nsmbw
        DEPENDS WiiCompiled
        COMMENT "Packing NSMBW-switch.zip"
        VERBATIM)
    add_custom_target(switch-symbols
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${MKW_SWITCH_SYMBOLS_STAGE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MKW_SWITCH_SYMBOLS_STAGE}"
        COMMAND "${CMAKE_OBJCOPY}" --only-keep-debug "$<TARGET_FILE:WiiCompiled>"
            "${MKW_SWITCH_SYMBOLS_STAGE}/NSMBW.debug"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${MKW_SWITCH_DIST_DIR}"
        COMMAND ${CMAKE_COMMAND} -E chdir "${MKW_SWITCH_SYMBOLS_STAGE}"
            ${CMAKE_COMMAND} -E tar cf "${MKW_SWITCH_DIST_DIR}/debug-symbols.zip"
            --format=zip NSMBW.debug
        DEPENDS WiiCompiled
        COMMENT "Packing debug-symbols.zip"
        VERBATIM)
    add_custom_target(switch-release DEPENDS switch-package switch-symbols)
endif()
if(MKW_PLATFORM_WINDOWS)
    # The app icon (projects/nsmbw/tools/make_icons.ps1): Explorer, the taskbar and, through SDL's
    # default of the executable's first icon, the game window. The manifest (1 24) gives the
    # first-start game picker Common Controls 6 and per-monitor DPI awareness from the start.
    enable_language(RC)
    file(TO_CMAKE_PATH "${MKW_RUNTIME_SOURCE_DIR}/assets/icon/nsmbw.ico" MKW_APP_ICON)
    file(TO_CMAKE_PATH "${MKW_RUNTIME_SOURCE_DIR}/assets/windows/NSMBW.manifest" MKW_APP_MANIFEST)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc"
        "1 ICON \"${MKW_APP_ICON}\"\n1 24 \"${MKW_APP_MANIFEST}\"\n")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc" APPEND PROPERTY
        OBJECT_DEPENDS "${MKW_APP_ICON}" "${MKW_APP_MANIFEST}")
    target_sources(WiiCompiled PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app_icon.rc")
endif()
if(TARGET mkw_base_sensitive)
    target_sources(WiiCompiled PRIVATE $<TARGET_OBJECTS:mkw_base_sensitive>)
endif()

# Dev only, not part of any build by default: extracts a disc image the way the game's Open ISO
# does (wii_disc.cpp), to compare with Dolphin's Extract Entire Disc.
add_executable(WiiDiscExtract EXCLUDE_FROM_ALL
    "${MKW_RUNTIME_SOURCE_DIR}/../projects/nsmbw/tools/WiiDiscExtract.cpp"
    "${MKW_RUNTIME_SOURCE_DIR}/src/wii_disc.cpp")
target_include_directories(WiiDiscExtract PRIVATE "${MKW_RUNTIME_SOURCE_DIR}/include")
target_compile_features(WiiDiscExtract PRIVATE cxx_std_20)
target_link_libraries(WiiDiscExtract PRIVATE mkw::cryptopp)

if(MKW_HAVE_RETRO_REWIND)
    add_executable(RetroRewind "${MKW_RETRO_REWIND_PRODUCT_SOURCE}" ${MKW_RETRO_REGISTRATION_SOURCES})
    mkw_configure_product(RetroRewind)
    target_precompile_headers(RetroRewind REUSE_FROM WiiCompiled)
    if(TARGET mkw_retro_sensitive)
        target_sources(RetroRewind PRIVATE $<TARGET_OBJECTS:mkw_retro_sensitive>)
    endif()
    target_sources(RetroRewind PRIVATE $<TARGET_OBJECTS:mkw_retro_rewind_functions>)
    if(MKW_RETRO_BLOB_OBJECTS)
        target_sources(RetroRewind PRIVATE ${MKW_RETRO_BLOB_OBJECTS})
    endif()
    add_custom_target(mkw_release DEPENDS WiiCompiled RetroRewind)
else()
    add_custom_target(mkw_release DEPENDS WiiCompiled)
    message(STATUS "RetroRewind target disabled (run translate-mod and emit-build-shards)")
endif()

# Windows and Linux x86_64 share the x86-64-v3 floor that the CPU baseline
# object above checks. AArch64 builds are compiled locally for the host that
# will run them, so both Linux and Apple Silicon use the compiler's native CPU
# tuning rather than leaving target-specific performance on the table.
# NSMBW: MKW_X86_LEVEL=v2 builds the "compat" product for x86 CPUs without AVX2/FMA (older than
# Haswell / Excavator). Fused multiply-add is then done in software (isa/ppc_soft_fma.h, bit-identical
# to the instruction), and the CPU check (host_cpu_baseline.cpp) asks only for x86-64-v2. The v3 build
# hands a v2-only CPU over to the compat build when it sits beside it as "<name>-compat".
set(MKW_X86_LEVEL "v3" CACHE STRING "x86-64 level of the product: v3, or v2 for the compat build")
set_property(CACHE MKW_X86_LEVEL PROPERTY STRINGS v3 v2)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|amd64|x86_64|X86_64)$")
    if(MKW_X86_LEVEL STREQUAL "v2")
        set(MKW_BASELINE_ARCH_FLAG -march=x86-64-v2)
        target_compile_definitions(mkw_cpu_baseline PRIVATE MKW_X86_LEVEL_V2=1)
    else()
        set(MKW_BASELINE_ARCH_FLAG -march=x86-64-v3)
    endif()
elseif(MKW_PLATFORM_ANDROID)
    # Cross-compiled, so -mcpu=native would describe the build machine. NSMBW: ARMv8.0-A, so phones
    # older than the Cortex-A55/A75 run it too (the Snapdragon 835's Kryo 280 has no LSE atomics and
    # took SIGILL on ldadd); outline atomics still use LSE where the CPU has it, picked at run time.
    set(MKW_BASELINE_ARCH_FLAG -march=armv8-a -moutline-atomics)
elseif(MKW_PLATFORM_SWITCH)
    # Same cross-compiled caveat as Android: the toolchain already tunes for
    # Cortex-A57, so no per-target arch flag.
    set(MKW_BASELINE_ARCH_FLAG "")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
    set(MKW_BASELINE_ARCH_FLAG -mcpu=native)
else()
    set(MKW_BASELINE_ARCH_FLAG "")
endif()

set(MKW_ALL_BUILD_TARGETS
    mkw_runtime_common mkw_base_shared mkw_base_sensitive mkw_retro_sensitive
    mkw_retro_rewind_functions WiiCompiled RetroRewind)
foreach(target IN LISTS MKW_ALL_BUILD_TARGETS)
    if(TARGET ${target} AND MKW_BASELINE_ARCH_FLAG)
        target_compile_options(${target} PRIVATE ${MKW_BASELINE_ARCH_FLAG})
    endif()
endforeach()
