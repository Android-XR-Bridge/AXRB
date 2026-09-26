cmake_minimum_required(VERSION 3.24)
project(quest-bridge LANGUAGES CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(OPENXR_DIR "${CMAKE_SOURCE_DIR}/third_party/openxr" CACHE PATH "OpenXR SDK directory")

if(ANDROID)
    add_library(qb_runtime SHARED runtime/android.cpp)
    target_include_directories(qb_runtime PRIVATE
        ${CMAKE_SOURCE_DIR}/protocol
        ${OPENXR_DIR}/include)
    return()
endif()

add_executable(qb-host host/main.cpp host/link.cpp)
add_executable(qb-source source/main.cpp)
add_library(qb_runtime SHARED runtime/runtime.cpp runtime/qb_runtime.def)
add_executable(qb-app app/main.cpp)
add_executable(qb-guest guest/runner.cpp guest/cpu.cpp guest/jit.cpp guest/jit_enter.asm guest/fp.cpp guest/crypto.cpp guest/atomic.cpp guest/glsl.cpp guest/loader.cpp guest/linker.cpp)
add_executable(qb-test guest/test.cpp guest/cpu.cpp guest/jit.cpp guest/jit_enter.asm guest/fp.cpp guest/crypto.cpp guest/atomic.cpp guest/loader.cpp guest/linker.cpp guest/android.cpp guest/threads.cpp guest/egl.cpp guest/jni.cpp guest/activity.cpp guest/vulkan.cpp guest/astc.cpp guest/openxr.cpp guest/xr_capture.cpp guest/platform.cpp guest/net.cpp guest/fastcalls.cpp guest/zlib.cpp guest/opensl.cpp guest/uithread.cpp)
target_include_directories(qb-host PRIVATE
    ${CMAKE_SOURCE_DIR}/protocol
    ${OPENXR_DIR}/include)
target_compile_definitions(qb-host PRIVATE
    XR_USE_PLATFORM_WIN32
    XR_USE_GRAPHICS_API_D3D11
    NOMINMAX
    WIN32_LEAN_AND_MEAN)
target_link_libraries(qb-host PRIVATE
    ${OPENXR_DIR}/native/x64/release/lib/openxr_loader.lib
    d3d11
    dxgi
    winmm)

target_include_directories(qb-source PRIVATE ${CMAKE_SOURCE_DIR}/protocol)
target_compile_definitions(qb-source PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(qb-source PRIVATE d3d11 dxgi d3dcompiler)

target_include_directories(qb_runtime PRIVATE
    ${CMAKE_SOURCE_DIR}/protocol
    ${OPENXR_DIR}/include)
target_compile_definitions(qb_runtime PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(qb_runtime PRIVATE d3d11 dxgi winmm)

target_include_directories(qb-app PRIVATE ${OPENXR_DIR}/include)
target_compile_definitions(qb-app PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(qb-app PRIVATE
    ${OPENXR_DIR}/native/x64/release/lib/openxr_loader.lib
    d3d11
    dxgi
    d3dcompiler)

target_include_directories(qb-guest PRIVATE
    ${CMAKE_SOURCE_DIR}/guest
    ${OPENXR_DIR}/include)
target_compile_definitions(qb-guest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(qb-guest PRIVATE
    ${OPENXR_DIR}/native/x64/release/lib/openxr_loader.lib
    d3d11
    dxgi
    d3dcompiler)

target_include_directories(qb-test PRIVATE ${CMAKE_SOURCE_DIR}/guest ${OPENXR_DIR}/include)
target_sources(qb-test PRIVATE guest/runner.manifest)
# Symbols for the Release build too (tools/hangdump.cpp names frozen threads' stacks).
target_compile_options(qb-test PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(qb-test PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)

set(QB_ANDROID_NDK "$ENV{ANDROID_NDK_HOME}" CACHE PATH "Android NDK for guest libraries")
set(ANDROID_CLANG "${QB_ANDROID_NDK}/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe")
if(NOT EXISTS "${ANDROID_CLANG}")
    message(FATAL_ERROR "Set QB_ANDROID_NDK or ANDROID_NDK_HOME to an installed Android NDK")
endif()
set(VULKAN_HEADERS "${QB_ANDROID_NDK}/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include")
# Copy only Vulkan headers, avoiding Android's C runtime headers on the host.
file(COPY "${VULKAN_HEADERS}/vulkan" "${VULKAN_HEADERS}/vk_video" DESTINATION "${CMAKE_BINARY_DIR}/vulkan-headers")
target_include_directories(qb-test PRIVATE "${CMAKE_BINARY_DIR}/vulkan-headers")
target_sources(qb-test PRIVATE guest/xr_layers.cpp)
set(LAYER_SHADER "${CMAKE_BINARY_DIR}/xr_layers.spv")
add_custom_command(OUTPUT "${LAYER_SHADER}"
    COMMAND "${QB_ANDROID_NDK}/shader-tools/windows-x86_64/glslc.exe"
        --target-env=vulkan1.0 -O "${CMAKE_SOURCE_DIR}/guest/xr_layers.comp" -o "${LAYER_SHADER}"
    DEPENDS guest/xr_layers.comp VERBATIM)
add_custom_target(xr_layer_shader DEPENDS "${LAYER_SHADER}")
add_dependencies(qb-test xr_layer_shader)
add_executable(qb-xr-layers-test guest/xr_layers_test.cpp)
target_include_directories(qb-xr-layers-test PRIVATE "${CMAKE_BINARY_DIR}/vulkan-headers" "${OPENXR_DIR}/include")
add_dependencies(qb-xr-layers-test xr_layer_shader)
add_dependencies(qb-test qb-xr-layers-test)
add_custom_command(TARGET qb-test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LAYER_SHADER}" $<TARGET_FILE_DIR:qb-test> VERBATIM)
set(GRID_SO ${CMAKE_BINARY_DIR}/libgrid.so)
add_custom_command(
    OUTPUT ${GRID_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -fno-builtin
        -I ${OPENXR_DIR}/include
        -Wl,--unresolved-symbols=ignore-all
        ${CMAKE_SOURCE_DIR}/guest/grid.c
        -o ${GRID_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/grid.c
    VERBATIM)
add_custom_target(grid_so DEPENDS ${GRID_SO})

# The real zlib, from the NDK's static archive, run as a guest library.
# Games inflate their APK entries through it, and reimplementing it on the
# host would be one more thing to get subtly wrong.
set(ZLIB_SO ${CMAKE_BINARY_DIR}/$<CONFIG>/libz.so)
add_custom_target(zlib_so
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/$<CONFIG>
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared
        -Wl,--whole-archive
        ${QB_ANDROID_NDK}/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libz.a
        -Wl,--no-whole-archive -Wl,-soname,libz.so
        -o ${ZLIB_SO}
    VERBATIM)
add_dependencies(qb-test zlib_so)

set(TEST_SO ${CMAKE_BINARY_DIR}/libneontest.so)
add_custom_command(
    OUTPUT ${TEST_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        -fno-stack-protector -fno-asynchronous-unwind-tables -nostdlib -fno-builtin
        -Wl,--unresolved-symbols=ignore-all
        ${CMAKE_SOURCE_DIR}/guest/neontest.c
        -o ${TEST_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/neontest.c
    VERBATIM)
add_custom_target(test_so DEPENDS ${TEST_SO})
add_dependencies(qb-test test_so)

# Built the ordinary way, linked against bionic, with no flags to avoid it.
set(LIBC_SO ${CMAKE_BINARY_DIR}/liblibctest.so)
add_custom_command(
    OUTPUT ${LIBC_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        ${CMAKE_SOURCE_DIR}/guest/libctest.c
        -o ${LIBC_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/libctest.c
    VERBATIM)
add_custom_target(libc_so DEPENDS ${LIBC_SO})
add_dependencies(qb-test libc_so)

# Two objects that have to be joined to each other at load time.
set(HELPER_SO ${CMAKE_BINARY_DIR}/libhelper.so)
add_custom_command(
    OUTPUT ${HELPER_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        -Wl,-soname,libhelper.so
        ${CMAKE_SOURCE_DIR}/guest/libhelper.c -o ${HELPER_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/libhelper.c
    VERBATIM)
set(LINK_SO ${CMAKE_BINARY_DIR}/liblinktest.so)
add_custom_command(
    OUTPUT ${LINK_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        ${CMAKE_SOURCE_DIR}/guest/linktest.c ${HELPER_SO} -o ${LINK_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/linktest.c ${HELPER_SO}
    VERBATIM)
add_custom_target(link_so DEPENDS ${LINK_SO})
add_dependencies(qb-test link_so)

set(TLS_SO ${CMAKE_BINARY_DIR}/libtlstest.so)
add_custom_command(
    OUTPUT ${TLS_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        ${CMAKE_SOURCE_DIR}/guest/tlstest.c -o ${TLS_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/tlstest.c
    VERBATIM)
add_custom_target(tls_so DEPENDS ${TLS_SO})
add_dependencies(qb-test tls_so)

set(THREAD_SO ${CMAKE_BINARY_DIR}/libthreadtest.so)
add_custom_command(
    OUTPUT ${THREAD_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        ${CMAKE_SOURCE_DIR}/guest/threadtest.c -o ${THREAD_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/threadtest.c
    VERBATIM)
add_custom_target(thread_so DEPENDS ${THREAD_SO})
add_dependencies(qb-test thread_so)

set(EGL_SO ${CMAKE_BINARY_DIR}/libegltest.so)
add_custom_command(
    OUTPUT ${EGL_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        ${CMAKE_SOURCE_DIR}/guest/egltest.c -lEGL -o ${EGL_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/egltest.c
    VERBATIM)
add_custom_target(egl_so DEPENDS ${EGL_SO})
add_dependencies(qb-test egl_so)

set(JNI_SO ${CMAKE_BINARY_DIR}/libjnitest.so)
add_custom_command(
    OUTPUT ${JNI_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        -Wl,--unresolved-symbols=ignore-all
        ${CMAKE_SOURCE_DIR}/guest/jnitest.c -o ${JNI_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/jnitest.c
    VERBATIM)
add_custom_target(jni_so DEPENDS ${JNI_SO})
add_dependencies(qb-test jni_so)

# A speed benchmark for the interpreter and the JIT.
set(BENCH_SO ${CMAKE_BINARY_DIR}/libbenchtest.so)
add_custom_command(
    OUTPUT ${BENCH_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        -Wl,--unresolved-symbols=ignore-all
        ${CMAKE_SOURCE_DIR}/guest/benchtest.c -o ${BENCH_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/benchtest.c
    VERBATIM)
add_custom_target(bench_so DEPENDS ${BENCH_SO})
add_dependencies(qb-test bench_so)

# Vulkan the way an engine reaches it, passed through to the host driver.
set(VK_SO ${CMAKE_BINARY_DIR}/libvktest.so)
add_custom_command(
    OUTPUT ${VK_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        -Wl,--unresolved-symbols=ignore-all -ldl
        ${CMAKE_SOURCE_DIR}/guest/vktest.c -o ${VK_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/vktest.c
    VERBATIM)
add_custom_target(vk_so DEPENDS ${VK_SO})
add_dependencies(qb-test vk_so)

# The NDK's own application glue, compiled in unmodified.
set(GLUE_DIR ${QB_ANDROID_NDK}/sources/android/native_app_glue)
set(GLUE_SO ${CMAKE_BINARY_DIR}/libgluetest.so)
add_custom_command(
    OUTPUT ${GLUE_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        -I ${GLUE_DIR} -Wl,--unresolved-symbols=ignore-all
        ${CMAKE_SOURCE_DIR}/guest/gluetest.c ${GLUE_DIR}/android_native_app_glue.c
        -o ${GLUE_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/gluetest.c
    VERBATIM)
add_custom_target(glue_so DEPENDS ${GLUE_SO})
add_dependencies(qb-test glue_so)

set(LIFECYCLE_SO ${CMAKE_BINARY_DIR}/liblifecycletest.so)
add_custom_command(OUTPUT ${LIFECYCLE_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        -Wl,--unresolved-symbols=ignore-all ${CMAKE_SOURCE_DIR}/guest/lifecycletest.c -o ${LIFECYCLE_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/lifecycletest.c VERBATIM)
add_custom_target(lifecycle_so DEPENDS ${LIFECYCLE_SO})
add_dependencies(qb-test lifecycle_so)
set(UNITYEXIT_SO ${CMAKE_BINARY_DIR}/libunityexittest.so)
add_custom_command(OUTPUT ${UNITYEXIT_SO}
    COMMAND ${ANDROID_CLANG} --target=aarch64-linux-android29 -shared -fPIC -O2
        -Wl,--unresolved-symbols=ignore-all ${CMAKE_SOURCE_DIR}/guest/unityexittest.c -o ${UNITYEXIT_SO}
    DEPENDS ${CMAKE_SOURCE_DIR}/guest/unityexittest.c VERBATIM)
add_custom_target(unityexit_so DEPENDS ${UNITYEXIT_SO})
add_dependencies(qb-test unityexit_so)
add_custom_command(TARGET qb-test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${UNITYEXIT_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIFECYCLE_SO} $<TARGET_FILE_DIR:qb-test> VERBATIM)

add_custom_command(TARGET qb-test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TEST_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LIBC_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HELPER_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LINK_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TLS_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${THREAD_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EGL_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${JNI_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GLUE_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VK_SO} $<TARGET_FILE_DIR:qb-test>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BENCH_SO} $<TARGET_FILE_DIR:qb-test>
    VERBATIM)
add_dependencies(qb-guest grid_so)

add_custom_command(TARGET qb-host POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENXR_DIR}/native/x64/release/bin/openxr_loader.dll
        $<TARGET_FILE_DIR:qb-host>
    VERBATIM)

add_custom_command(TARGET qb-guest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENXR_DIR}/native/x64/release/bin/openxr_loader.dll
        $<TARGET_FILE_DIR:qb-guest>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/runtime/qb_runtime.json
        $<TARGET_FILE_DIR:qb-guest>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${GRID_SO}
        $<TARGET_FILE_DIR:qb-guest>
    VERBATIM)

add_custom_command(TARGET qb-app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENXR_DIR}/native/x64/release/bin/openxr_loader.dll
        $<TARGET_FILE_DIR:qb-app>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/runtime/qb_runtime.json
        $<TARGET_FILE_DIR:qb-app>
    VERBATIM)

# Native stacks of a frozen qb-test (tools/hangdump.cpp).
add_executable(hangdump tools/hangdump.cpp)

# The application runner reaches the system OpenXR runtime directly.
add_custom_command(TARGET qb-test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${OPENXR_DIR}/native/x64/release/bin/openxr_loader.dll $<TARGET_FILE_DIR:qb-test>
    VERBATIM)
enable_testing()
foreach(probe neontest libctest linktest tlstest threadtest egltest jnitest gluetest)
    add_test(NAME qb-${probe} COMMAND qb-test "${CMAKE_BINARY_DIR}/$<CONFIG>/lib${probe}.so")
    set_tests_properties(qb-${probe} PROPERTIES TIMEOUT 60)
    add_test(NAME qb-${probe}-interpreter COMMAND qb-test "${CMAKE_BINARY_DIR}/$<CONFIG>/lib${probe}.so")
    set_tests_properties(qb-${probe}-interpreter PROPERTIES TIMEOUT 60 ENVIRONMENT "QB_JIT=0")
endforeach()
add_test(NAME qb-jit-differential COMMAND qb-test)
set_tests_properties(qb-jit-differential PROPERTIES TIMEOUT 60 ENVIRONMENT "QB_JITFUZZ=5000")
add_test(NAME qb-unity-exit COMMAND qb-test "${CMAKE_BINARY_DIR}/$<CONFIG>/libunityexittest.so" JNI_OnLoad)
set_tests_properties(qb-unity-exit PROPERTIES TIMEOUT 5 ENVIRONMENT "QB_DRIVE=1000" PASS_REGULAR_EXPRESSION "1 passed, 0 failed")
add_test(NAME qb-xr-layers COMMAND qb-xr-layers-test)
set_tests_properties(qb-xr-layers PROPERTIES TIMEOUT 30 PASS_REGULAR_EXPRESSION "GPU compositor shader and frame translation checks passed")
