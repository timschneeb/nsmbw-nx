# Switch graphics stack: Aurora GX -> Dawn Vulkan -> Mesa NVK (Nouveau Tegra X1).
# Dawn and Mesa are prebuilt once outside this configure and imported here.
#
# Dawn (encounter/dawn, ref 1155e0ed which already carries Horizon support):
#   cmake -S <dawn> -B <build> --toolchain runtime/cmake/SwitchToolchain.cmake \
#     -DDAWN_PLATFORM_SWITCH=ON -DDAWN_ENABLE_VULKAN=ON \
#     -DDAWN_ENABLE_D3D11=OFF -DDAWN_ENABLE_D3D12=OFF -DDAWN_ENABLE_METAL=OFF \
#     -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF \
#     -DDAWN_ENABLE_NULL=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_BUILD_PROTOBUF=OFF \
#     -DDAWN_FETCH_DEPENDENCIES=ON \
#     -DTINT_BUILD_AS_OTHER_OS=ON -DTINT_BUILD_SPV_READER=ON \
#     -DTINT_BUILD_SPV_WRITER=ON -DTINT_BUILD_WGSL_READER=ON \
#     -DTINT_BUILD_WGSL_WRITER=ON -DTINT_BUILD_GLSL_WRITER=OFF \
#     -DTINT_BUILD_HLSL_WRITER=OFF -DTINT_BUILD_MSL_WRITER=OFF \
#     -DTINT_BUILD_IR_BINARY=OFF
#   cmake --build <build> --target webgpu_dawn
#
# Mesa (danfromtico/mesa-switch build-switch.sh recipe: native mesa_clc +
# vtn_bindgen2, then cross build with MESA_SWITCH_RUST_TARGET set so the NAK
# Rust crates compile against the prebuilt aarch64-linux std):
#   meson setup builddir-switch --wipe --cross-file <local cross file> \
#     --buildtype=release -Dvulkan-drivers=nouveau -Dgallium-drivers=nouveau \
#     -Dplatforms=switch -Dllvm=disabled -Dmesa-clc=system \
#     -Dprecomp-compiler=system [...]; ninja -C builddir-switch <archives below>
set(MKW_DAWN_SWITCH_BUILD "" CACHE PATH "Dawn Switch build dir (webgpu_dawn + gen headers)")
set(MKW_DAWN_SWITCH_SOURCE "" CACHE PATH "Dawn source dir matching the build (webgpu/webgpu.h)")
set(MKW_MESA_SWITCH_BUILD "" CACHE PATH "Mesa Switch build dir (builddir-switch with NVK archives)")

if (NOT MKW_DAWN_SWITCH_BUILD OR NOT EXISTS "${MKW_DAWN_SWITCH_BUILD}/src/dawn/native/libwebgpu_dawn.a")
    message(FATAL_ERROR "MKW_DAWN_SWITCH_BUILD must point at a Dawn Switch build dir containing src/dawn/native/libwebgpu_dawn.a (see the recipe at the top of runtime/cmake/SwitchGraphics.cmake)")
endif()
if (NOT MKW_DAWN_SWITCH_SOURCE OR NOT EXISTS "${MKW_DAWN_SWITCH_SOURCE}/include/webgpu/webgpu.h")
    message(FATAL_ERROR "MKW_DAWN_SWITCH_SOURCE must point at the Dawn source dir matching the build (include/webgpu/webgpu.h)")
endif()
if (NOT MKW_MESA_SWITCH_BUILD OR NOT EXISTS "${MKW_MESA_SWITCH_BUILD}/src/nouveau/vulkan/libvulkan.a")
    message(FATAL_ERROR "MKW_MESA_SWITCH_BUILD must point at a Mesa Switch build dir containing src/nouveau/vulkan/libvulkan.a (see the recipe at the top of runtime/cmake/SwitchGraphics.cmake)")
endif()

# libvulkan.a is the loaderless superset (driver + loader + dispatch): linking
# libnvk.a as well duplicates every symbol. z/zstd/expat come from portlibs.
set(_mkw_mesa_archives
    src/nouveau/vulkan/libvulkan.a
    src/nouveau/horizon/libnouveau_horizon.a
    src/vulkan/util/libvulkan_util.a
    src/compiler/nir/libnir.a
    src/compiler/libcompiler.a
    src/compiler/spirv/libvtn.a
    src/util/libxmlconfig.a
    src/nouveau/compiler/libnak.a
    src/nouveau/compiler/libnak_rs.a
    src/nouveau/nil/libnil.a
    src/nouveau/nil/liblibnil_format_table.a
    src/nouveau/mme/libnouveau_mme.a
    src/nouveau/winsys/libnouveau_ws.a
    src/nouveau/headers/libnvidia_headers_c.a
    src/compiler/rust/libcompiler_c_helpers.a
    src/util/libmesa_util.a
    src/util/libmesa_util_simd.a
    src/util/blake3/libblake3.a
    src/c11/impl/libmesa_util_c11.a)
set(_mkw_mesa_paths "")
foreach(_archive IN LISTS _mkw_mesa_archives)
    if (NOT EXISTS "${MKW_MESA_SWITCH_BUILD}/${_archive}")
        message(FATAL_ERROR "Mesa Switch build is missing ${_archive}; finish the builddir-switch build first")
    endif()
    list(APPEND _mkw_mesa_paths "${MKW_MESA_SWITCH_BUILD}/${_archive}")
endforeach()

set(_mkw_portlib_dir "$ENV{DEVKITPRO}/portlibs/switch/lib")
foreach(_portlib libexpat.a libz.a libzstd.a)
    if (NOT EXISTS "${_mkw_portlib_dir}/${_portlib}")
        message(FATAL_ERROR "Switch portlib ${_portlib} not found in ${_mkw_portlib_dir}")
    endif()
endforeach()

add_library(mkw_switch_graphics INTERFACE IMPORTED)
set_target_properties(mkw_switch_graphics PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${MKW_DAWN_SWITCH_BUILD}/gen/include;${MKW_DAWN_SWITCH_SOURCE}/include"
    INTERFACE_COMPILE_DEFINITIONS "VK_USE_PLATFORM_VI_NN"
    INTERFACE_LINK_LIBRARIES "${MKW_DAWN_SWITCH_BUILD}/src/dawn/native/libwebgpu_dawn.a;-Wl,--start-group;${_mkw_mesa_paths};-Wl,--end-group;${_mkw_portlib_dir}/libexpat.a;${_mkw_portlib_dir}/libz.a;${_mkw_portlib_dir}/libzstd.a")
