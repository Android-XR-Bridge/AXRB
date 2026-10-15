cmake_minimum_required(VERSION 3.24)
project(axrb_native_x64 LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "The native x64 prototype requires Windows x64")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYAN_ZYCORE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/dynarmic/externals/zycore" CACHE PATH "" FORCE)
add_subdirectory(../../third_party/dynarmic/externals/zydis ${CMAKE_BINARY_DIR}/zydis EXCLUDE_FROM_ALL)
add_executable(axrb-native-x64 main.cpp files.cpp tls_bridge.cpp arm_bridge.cpp arm_elf.cpp)
target_compile_definitions(axrb-native-x64 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(axrb-native-x64 PRIVATE /W4 /EHsc)
target_link_options(axrb-native-x64 PRIVATE "/MAP:${CMAKE_BINARY_DIR}/axrb-native-x64.map")
target_link_libraries(axrb-native-x64 PRIVATE Zydis)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(AXRB_NDK "$ENV{LOCALAPPDATA}/Android/Sdk/ndk/29.0.14206865" CACHE PATH "Developer NDK")
set(CLANG "${AXRB_NDK}/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe")
get_filename_component(AXRB_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
enable_testing()
add_executable(native-files-test tests/files_test.cpp files.cpp)
target_compile_definitions(native-files-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_test(NAME native-x64-saves COMMAND native-files-test)
add_subdirectory(../digitalis ${CMAKE_BINARY_DIR}/digitalis)
target_link_libraries(axrb-native-x64 PRIVATE axrb-digitalis)
set(PROBE "${CMAKE_BINARY_DIR}/native-probe")
add_custom_command(OUTPUT "${PROBE}"
  COMMAND "${CLANG}" --target=x86_64-linux-android29 -O2 -Wall -Werror
    -fPIE -pie -nostdlib -fno-stack-protector -mno-red-zone -fno-builtin
    -Wl,--no-dynamic-linker -Wl,-e,_start
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/start.S" "${CMAKE_CURRENT_SOURCE_DIR}/tests/probe.c" -o "${PROBE}"
  DEPENDS tests/start.S tests/probe.c VERBATIM)
add_custom_target(native-probe ALL DEPENDS "${PROBE}")
set(BIONIC_PROBE "${CMAKE_BINARY_DIR}/bionic-probe")
add_custom_command(OUTPUT "${BIONIC_PROBE}"
  COMMAND "${CLANG}" --target=x86_64-linux-android29 -O2 -Wall -Werror -mno-red-zone
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/bionic.c" -ldl -o "${BIONIC_PROBE}"
  DEPENDS tests/bionic.c VERBATIM)
add_custom_target(bionic-probe ALL DEPENDS "${BIONIC_PROBE}")
set(PTHREAD_PROBE "${CMAKE_BINARY_DIR}/pthread-probe")
add_custom_command(OUTPUT "${PTHREAD_PROBE}"
  COMMAND "${CLANG}" --target=x86_64-linux-android29 -O2 -Wall -Werror -mno-red-zone
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/pthread.c" -o "${PTHREAD_PROBE}"
  DEPENDS tests/pthread.c VERBATIM)
add_custom_target(pthread-probe ALL DEPENDS "${PTHREAD_PROBE}")
set(JNI_PROBE "${CMAKE_BINARY_DIR}/libnative-art-probe.so")
add_custom_command(OUTPUT "${JNI_PROBE}"
  COMMAND "${CLANG}" --target=x86_64-linux-android29 -O2 -Wall -Werror -fPIC -shared
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/art_jni.c" -o "${JNI_PROBE}"
  DEPENDS tests/art_jni.c VERBATIM)
add_custom_target(art-jni-probe ALL DEPENDS "${JNI_PROBE}")
set(ARM_TRAMPOLINES "${CMAKE_BINARY_DIR}/arm-trampolines.S")
add_custom_command(OUTPUT "${ARM_TRAMPOLINES}"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/generate_arm_trampolines.py" "${ARM_TRAMPOLINES}"
  DEPENDS generate_arm_trampolines.py VERBATIM)
set(ARM_SHIM "${CMAKE_BINARY_DIR}/libarm-jni-bridge.so")
add_custom_command(OUTPUT "${ARM_SHIM}"
  COMMAND "${CLANG}" --target=x86_64-linux-android29 -O2 -Wall -Werror -fPIC -shared
    "${CMAKE_CURRENT_SOURCE_DIR}/arm_jni_bridge.c" "${CMAKE_CURRENT_SOURCE_DIR}/tests/arm_register.c"
    "${ARM_TRAMPOLINES}" -landroid -o "${ARM_SHIM}"
  DEPENDS arm_jni_bridge.c arm_jni_bridge.h arm_bridge_abi.h tests/arm_register.c "${ARM_TRAMPOLINES}" VERBATIM)
set(ARM_FIXTURE "${CMAKE_BINARY_DIR}/libarm-jni-fixture.so")
add_custom_command(OUTPUT "${ARM_FIXTURE}"
  COMMAND "${CLANG}" --target=aarch64-linux-android29 -O2 -Wall -Werror -fPIC -shared
    -nostdlib -fno-stack-protector -fno-builtin -Wl,--pack-dyn-relocs=none
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/arm_jni_fixture.c" -o "${ARM_FIXTURE}"
  DEPENDS tests/arm_jni_fixture.c VERBATIM)
add_custom_target(arm-jni-probe ALL DEPENDS "${ARM_SHIM}" "${ARM_FIXTURE}")
set(NATIVE_BRIDGE "${CMAKE_BINARY_DIR}/libaxrb_nativebridge.so")
add_custom_command(OUTPUT "${NATIVE_BRIDGE}"
  COMMAND "${CLANG}" --target=x86_64-linux-android29 -O2 -Wall -Werror -fPIC -shared
    "${CMAKE_CURRENT_SOURCE_DIR}/arm_jni_bridge.c" "${CMAKE_CURRENT_SOURCE_DIR}/native_bridge_adapter.cpp"
    "${ARM_TRAMPOLINES}" -landroid -o "${NATIVE_BRIDGE}"
  DEPENDS arm_jni_bridge.c arm_jni_bridge.h arm_bridge_abi.h native_bridge_adapter.cpp "${ARM_TRAMPOLINES}" VERBATIM)
add_custom_target(android-native-bridge ALL DEPENDS "${NATIVE_BRIDGE}")
set(PLATFORM "${CMAKE_BINARY_DIR}/platform")
add_custom_command(OUTPUT "${PLATFORM}/libaxrb_art_bridge.so" "${PLATFORM}/libaxrb_art_bridge.so.axrb-code"
    "${PLATFORM}/arm64/libandroid.so" "${PLATFORM}/arm64/libOpenSLES.so"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build_platform.py"
    --ndk "${AXRB_NDK}" --output "${PLATFORM}"
  DEPENDS build_platform.py code_ranges.py ../art/art_bridge.c ../art/art_binder.cpp ../art/art_media.c
    ../guest/proxy/libandroid.c ../guest/proxy/libOpenSLES.c ../guest/proxy/opensles_output.inc
  VERBATIM)
add_custom_target(native-x64-platform ALL DEPENDS "${PLATFORM}/libaxrb_art_bridge.so"
  "${PLATFORM}/libaxrb_art_bridge.so.axrb-code" "${PLATFORM}/arm64/libandroid.so" "${PLATFORM}/arm64/libOpenSLES.so")
set(ARM_BOUNDARY "${CMAKE_BINARY_DIR}/arm-boundary")
add_custom_command(OUTPUT "${ARM_BOUNDARY}"
  COMMAND "${CLANG}" --target=x86_64-linux-android29 -O2 -Wall -Werror
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/arm_boundary.c" -o "${ARM_BOUNDARY}"
  DEPENDS tests/arm_boundary.c arm_bridge_abi.h VERBATIM)
add_custom_target(arm-boundary ALL DEPENDS "${ARM_BOUNDARY}")
enable_testing()
add_test(NAME native-x64-boundary COMMAND "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native.py" "$<TARGET_FILE:axrb-native-x64>" "${PROBE}")
set_tests_properties(native-x64-boundary PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME native-x64-sysroot COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sysroot.py")
set(AXRB_X64_SYSROOT "" CACHE PATH "Optional prepared Android x86_64 sysroot for integration tests")
set(AXRB_ANDROID_BUILD_TOOLS "$ENV{LOCALAPPDATA}/Android/Sdk/build-tools/36.0.0" CACHE PATH "Developer D8 for ART probe")
if(AXRB_X64_SYSROOT)
  foreach(kind bionic pthread)
    string(TOUPPER "${kind}" upper)
    add_test(NAME native-x64-${kind} COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_integration.py" "$<TARGET_FILE:axrb-native-x64>"
      "${AXRB_X64_SYSROOT}" "${CMAKE_BINARY_DIR}/${kind}-probe" "NATIVE_X64_${upper}_PASS")
    set_tests_properties(native-x64-${kind} PROPERTIES SKIP_RETURN_CODE 77)
  endforeach()
  add_test(NAME native-x64-art COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run_art_probe.py" --runner "$<TARGET_FILE:axrb-native-x64>"
    --sysroot "${AXRB_X64_SYSROOT}" --work "${CMAKE_BINARY_DIR}" --build-tools "${AXRB_ANDROID_BUILD_TOOLS}")
  set_tests_properties(native-x64-art PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME native-x64-arm-jni COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run_art_probe.py" --runner "$<TARGET_FILE:axrb-native-x64>"
    --sysroot "${AXRB_X64_SYSROOT}" --work "${CMAKE_BINARY_DIR}" --build-tools "${AXRB_ANDROID_BUILD_TOOLS}" --arm)
  set_tests_properties(native-x64-arm-jni PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
  add_test(NAME native-x64-nativebridge COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run_art_probe.py" --runner "$<TARGET_FILE:axrb-native-x64>"
    --sysroot "${AXRB_X64_SYSROOT}" --work "${CMAKE_BINARY_DIR}" --build-tools "${AXRB_ANDROID_BUILD_TOOLS}" --native-bridge)
  set_tests_properties(native-x64-nativebridge PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
  add_test(NAME native-x64-arm-boundary COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arm_boundary.py" "$<TARGET_FILE:axrb-native-x64>"
    "${AXRB_X64_SYSROOT}" "${ARM_BOUNDARY}" "${ARM_FIXTURE}")
  set_tests_properties(native-x64-arm-boundary PROPERTIES SKIP_RETURN_CODE 77)
endif()

set(ARM_LINK_DEP "${CMAKE_BINARY_DIR}/libarm-link-dep.so")
set(ARM_LINK_PARENT "${CMAKE_BINARY_DIR}/libarm-link-parent.so")
add_custom_command(OUTPUT "${ARM_LINK_DEP}"
  COMMAND "${CLANG}" --target=aarch64-linux-android29 -O2 -Wall -Werror -fPIC -shared -nostdlib -fno-stack-protector
    -Wl,--pack-dyn-relocs=none -Wl,-soname,libarm-link-dep.so
    "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/tests/arm_link_dep.map"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/arm_link_dep.c" -o "${ARM_LINK_DEP}"
  DEPENDS tests/arm_link_dep.c tests/arm_link_dep.map VERBATIM)
add_custom_command(OUTPUT "${ARM_LINK_PARENT}"
  COMMAND "${CLANG}" --target=aarch64-linux-android29 -O2 -Wall -Werror -fPIC -shared -nostdlib -fno-stack-protector
    -Wl,--pack-dyn-relocs=none "${CMAKE_CURRENT_SOURCE_DIR}/tests/arm_link_parent.c" "${ARM_LINK_DEP}" -o "${ARM_LINK_PARENT}"
  DEPENDS tests/arm_link_parent.c "${ARM_LINK_DEP}" VERBATIM)
add_custom_target(arm-link-fixtures ALL DEPENDS "${ARM_LINK_PARENT}")
add_executable(arm-elf-test tests/arm_elf_test.cpp arm_elf.cpp)
target_compile_definitions(arm-elf-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
add_dependencies(arm-elf-test arm-link-fixtures)
add_test(NAME native-x64-arm-linker COMMAND arm-elf-test "${ARM_LINK_PARENT}" "${ARM_LINK_DEP}")
if(AXRB_X64_SYSROOT)
  add_test(NAME native-x64-arm-link-integration COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run_art_probe.py" --runner "$<TARGET_FILE:axrb-native-x64>"
    --sysroot "${AXRB_X64_SYSROOT}" --work "${CMAKE_BINARY_DIR}" --build-tools "${AXRB_ANDROID_BUILD_TOOLS}" --linker)
  set_tests_properties(native-x64-arm-link-integration PROPERTIES SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
endif()

set(ARM_BAD_INIT "${CMAKE_BINARY_DIR}/libarm-link-bad-init.so")
add_custom_command(OUTPUT "${ARM_BAD_INIT}"
  COMMAND "${CLANG}" --target=aarch64-linux-android29 -O2 -Wall -Werror -fPIC -shared -nostdlib -fno-stack-protector
    -Wl,--pack-dyn-relocs=none "${CMAKE_CURRENT_SOURCE_DIR}/tests/arm_link_bad_init.c" -o "${ARM_BAD_INIT}"
  DEPENDS tests/arm_link_bad_init.c VERBATIM)
add_custom_target(arm-link-bad-init ALL DEPENDS "${ARM_BAD_INIT}")
foreach(format android android+relr)
  set(packed "${CMAKE_BINARY_DIR}/libarm-link-${format}.so")
  add_custom_command(OUTPUT "${packed}"
    COMMAND "${CLANG}" --target=aarch64-linux-android29 -O2 -Wall -Werror -fPIC -shared -nostdlib -fno-stack-protector
      "-Wl,--pack-dyn-relocs=${format}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/arm_link_parent.c" "${ARM_LINK_DEP}" -o "${packed}"
    DEPENDS tests/arm_link_parent.c "${ARM_LINK_DEP}" VERBATIM)
  add_custom_target(arm-link-${format} ALL DEPENDS "${packed}")
  add_dependencies(arm-elf-test arm-link-${format})
  add_test(NAME native-x64-arm-linker-${format} COMMAND arm-elf-test "${packed}" "${ARM_LINK_DEP}")
endforeach()
