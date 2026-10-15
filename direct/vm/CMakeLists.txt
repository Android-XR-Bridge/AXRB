# The managed side of an Android app: Androitainer's DEX interpreter, core
# library and framework subset (snapshot, see SNAPSHOT.md), built as static
# libraries for axrb-direct.
set(AXRB_VM_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_SAVED "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/vm")
endif()

add_compile_options(/utf-8 /EHsc /GR /W3 /wd4100 /wd4244 /wd4267 /wd4146)
add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)

add_subdirectory(diagnostics)
add_subdirectory(vfs)
add_subdirectory(package)
add_subdirectory(manifest)
add_subdirectory(resources)
add_subdirectory(managed)
add_subdirectory(framework-host)
