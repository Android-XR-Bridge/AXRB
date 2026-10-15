cmake_minimum_required(VERSION 3.24)
project(axrb_direct LANGUAGES C CXX)

# AXRB Direct: runs an Android ARM64 application in an ordinary Windows
# process. Guest code executes through Dynarmic; the Android userspace is the
# unmodified AOSP arm64 bionic/linker from the SDK system image, with Linux
# system calls answered here.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

get_filename_component(AXRB_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(DYNARMIC_SOURCE "${AXRB_ROOT}/third_party/dynarmic" CACHE PATH "Pinned Dynarmic checkout")
set(BOOST_ROOT "${AXRB_ROOT}/third_party/boost_1_84_0" CACHE PATH "Boost headers for Dynarmic")
set(Boost_INCLUDE_DIR "${BOOST_ROOT}" CACHE PATH "")
set(Boost_NO_BOOST_CMAKE ON CACHE BOOL "")

set(DYNARMIC_FRONTENDS A64 CACHE STRING "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_POLICY_DEFAULT_CMP0167 OLD)
add_subdirectory("${DYNARMIC_SOURCE}" dynarmic EXCLUDE_FROM_ALL)

set(AXRB_NDK "$ENV{ANDROID_NDK_HOME}" CACHE PATH "Android NDK used to build the guest-side helpers")
if(NOT AXRB_NDK)
  file(GLOB _ndks "$ENV{LOCALAPPDATA}/Android/Sdk/ndk/*")
  list(SORT _ndks COMPARE NATURAL ORDER DESCENDING)
  list(GET _ndks 0 AXRB_NDK)
endif()
set(NDK_BIN "${AXRB_NDK}/toolchains/llvm/prebuilt/windows-x86_64/bin")
if(NOT EXISTS "${NDK_BIN}/clang.exe")
  message(FATAL_ERROR "AXRB_NDK must name an installed Android NDK (found '${AXRB_NDK}')")
endif()

# Vulkan headers only (never the Android C library headers on the host).
set(_ndk_include "${AXRB_NDK}/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include")
file(COPY "${_ndk_include}/vulkan" "${_ndk_include}/vk_video" DESTINATION "${CMAKE_BINARY_DIR}/vulkan-headers")

add_subdirectory(guest)
enable_testing()
add_executable(axrb-time-math-test tests/time_math.cpp)
target_include_directories(axrb-time-math-test PRIVATE src)
add_test(NAME time-math COMMAND axrb-time-math-test)
add_subdirectory(digitalis)
option(AXRB_BUILD_LEGACY_BOOT "Build the former experimental managed interpreter boot DEX" OFF)
add_subdirectory(vm)

add_executable(axrb-direct
  src/main.cpp
  src/log.cpp
  src/memory.cpp
  src/cpu.cpp
  src/cpu_engine.cpp
  src/fallback.cpp
  src/kernel.cpp
  src/vfs.cpp
  src/files.cpp
  src/binder.cpp
  src/threads.cpp
  src/signals.cpp
  src/net.cpp
  src/exec.cpp
  src/sysroot.cpp
  src/props.cpp
  src/app.cpp
  src/jni.cpp
  src/java_vfs.cpp
  src/natives.cpp
  src/ndk_android.cpp
  src/audio.cpp
  src/vk_host.cpp
  src/vk_texture.cpp
  src/media.cpp
  src/media_codec.cpp
  src/profiler.cpp
  src/window.cpp
)
target_include_directories(axrb-direct PRIVATE src "${CMAKE_BINARY_DIR}/vulkan-headers" "${AXRB_ROOT}/protocol")
# The emulator layer's texture transcoding (runtime/vulkan/texture), built for the
# host with Android header shims. bc7e links the prebuilt Windows objects ISPC
# makes from bc7e.ispc (scripts/build/build_bc7e.py), so no ISPC is needed here.
set_source_files_properties(src/vk_texture.cpp PROPERTIES INCLUDE_DIRECTORIES
    "${CMAKE_CURRENT_LIST_DIR}/src/compat;${AXRB_ROOT}/runtime/vulkan")
set(_bc7e_dir "${AXRB_ROOT}/runtime/vulkan/texture/bc7e/win64")
target_sources(axrb-direct PRIVATE "${_bc7e_dir}/bc7e.obj" "${_bc7e_dir}/bc7e_sse4.obj" "${_bc7e_dir}/bc7e_avx2.obj")
target_compile_definitions(axrb-direct PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
target_compile_options(axrb-direct PRIVATE /W3 /utf-8 /EHa /Zi /MP)
target_link_options(axrb-direct PRIVATE /DEBUG /OPT:REF /OPT:ICF /STACK:16777216)
target_link_libraries(axrb-direct PRIVATE arun_framework_host arun_managed arun_resources arun_manifest arun_package arun_vfs arun_diagnostics dynarmic axrb-digitalis ws2_32 bcrypt ntdll synchronization winmm dbghelp onecore psapi shell32 ole32 mfplat mfreadwrite mfuuid shlwapi)
add_dependencies(axrb-direct axrb_guest)
if(AXRB_BUILD_LEGACY_BOOT)
  add_dependencies(axrb-direct arun_boot_dex)
endif()

# AXRB's OpenXR runtime (arm64, built with the NDK) and the host GPU-share layer,
# installed beside the runner when they have been built.
set(AXRB_OPENXR_RUNTIME_SO "${AXRB_ROOT}/build/runtime-arm64/runtime/src/libopenxr_runtime.so" CACHE FILEPATH
    "arm64 libopenxr_runtime.so for the guest")
set(AXRB_GPU_LAYER_DLL "${AXRB_ROOT}/build/gpu/Release/axrb_gpu_layer.dll" CACHE FILEPATH
    "Host GPU-share Vulkan layer")
if(EXISTS "${AXRB_OPENXR_RUNTIME_SO}")
  add_custom_command(TARGET axrb-direct POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AXRB_OPENXR_RUNTIME_SO}" "${CMAKE_BINARY_DIR}/guest/lib64/libopenxr_runtime.so")
endif()
if(EXISTS "${AXRB_GPU_LAYER_DLL}")
  file(WRITE "${CMAKE_BINARY_DIR}/axrb_gpu_layer.json"
    "{\"file_format_version\":\"1.2.0\",\"layer\":{\"name\":\"VK_LAYER_AXRB_gpu_share\",\"type\":\"GLOBAL\",\"library_path\":\".\\\\axrb_gpu_layer.dll\",\"api_version\":\"1.3.0\",\"implementation_version\":\"1\",\"description\":\"AXRB Windows shared eye textures\"}}
")
  add_custom_command(TARGET axrb-direct POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:axrb-direct>/gpu"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AXRB_GPU_LAYER_DLL}" "$<TARGET_FILE_DIR:axrb-direct>/gpu/axrb_gpu_layer.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_BINARY_DIR}/axrb_gpu_layer.json" "$<TARGET_FILE_DIR:axrb-direct>/gpu/axrb_gpu_layer.json")
endif()

# Developer-only embedding of the proven ARM userspace for native x86 ART.
# Kept out of normal launcher/runtime builds until mixed-ISA game tests pass.
option(AXRB_BUILD_ARM_SERVICE "Build experimental embedded ARM service" OFF)
if(AXRB_BUILD_ARM_SERVICE)
  get_target_property(_arm_sources axrb-direct SOURCES)
  list(REMOVE_ITEM _arm_sources src/main.cpp)
  add_library(axrb-arm-service SHARED ${_arm_sources} x64/arm_service.cpp)
  get_target_property(_arm_includes axrb-direct INCLUDE_DIRECTORIES)
  get_target_property(_arm_libraries axrb-direct LINK_LIBRARIES)
  target_include_directories(axrb-arm-service PRIVATE ${_arm_includes})
  target_compile_definitions(axrb-arm-service PRIVATE AXRB_ARM_SERVICE_EMBEDDED WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
  target_compile_options(axrb-arm-service PRIVATE /W3 /utf-8 /EHa /Zi /MP)
  target_link_options(axrb-arm-service PRIVATE /DEBUG /OPT:REF /OPT:ICF)
  target_link_libraries(axrb-arm-service PRIVATE ${_arm_libraries})
  add_executable(arm-service-test x64/tests/arm_service_test.cpp)
  target_link_libraries(arm-service-test PRIVATE axrb-arm-service)
  target_link_options(arm-service-test PRIVATE /STACK:16777216)
  add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/native-arm-helper"
    COMMAND "${NDK_BIN}/clang.exe" --target=aarch64-linux-android29 -O2 -Wall -Werror
      "${CMAKE_CURRENT_SOURCE_DIR}/x64/arm_service_guest.c" -ldl -o "${CMAKE_BINARY_DIR}/native-arm-helper"
    DEPENDS x64/arm_service_guest.c VERBATIM)
  add_custom_target(native-arm-helper ALL DEPENDS "${CMAKE_BINARY_DIR}/native-arm-helper")
endif()
