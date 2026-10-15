find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(DIGITALIS_SOURCE "${AXRB_ROOT}/third_party/digitalis")
if(NOT EXISTS "${DIGITALIS_SOURCE}/interpreter/arm64/interpreter.h")
  message(FATAL_ERROR "Digitalis sources missing. Run scripts/build/direct_deps.ps1.")
endif()
set(_digitalis_out "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
file(GLOB_RECURSE _digitalis_dependencies CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.asm" "${DIGITALIS_SOURCE}/*.h"
  "${DIGITALIS_SOURCE}/*.inc" "${DIGITALIS_SOURCE}/*.cc"
  "${DIGITALIS_SOURCE}/*.py" "${DIGITALIS_SOURCE}/*.json")
add_custom_command(OUTPUT "${_digitalis_out}/digitalis.lib" "${_digitalis_out}/digitalis-selftest.exe"
  "${_digitalis_out}/digitalis-differential.exe"
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/build.py"
    --heavy --ndk "${AXRB_NDK}" --linker "${CMAKE_LINKER}" --out "${_digitalis_out}" --config "$<CONFIG>"
  DEPENDS ${_digitalis_dependencies} "${CMAKE_CURRENT_SOURCE_DIR}/build.py" "${CMAKE_CURRENT_SOURCE_DIR}/build_heavy.py"
  COMMENT "Building Digitalis Windows optimizing JIT, lite JIT and interpreter" VERBATIM)
add_custom_target(axrb-digitalis-build DEPENDS "${_digitalis_out}/digitalis.lib")
add_library(axrb-digitalis INTERFACE)
add_dependencies(axrb-digitalis axrb-digitalis-build)
target_link_libraries(axrb-digitalis INTERFACE "${_digitalis_out}/digitalis.lib")
add_test(NAME axrb_digitalis_windows COMMAND "${_digitalis_out}/digitalis-selftest.exe")
add_test(NAME axrb_digitalis_differential COMMAND "${_digitalis_out}/digitalis-differential.exe")
