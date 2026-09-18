set(AXRB_RUNTIME_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/state.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/instance.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/session.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/spaces.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/input.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/hand_tracking.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/swapchain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/frame_loop.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/frame_transport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/dispatch.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/openxr_dispatch/runtime_entry.cpp
)
set(AXRB_RUNTIME_SOURCES ${AXRB_RUNTIME_SOURCES} PARENT_SCOPE)

add_library(openxr_runtime SHARED ${AXRB_RUNTIME_SOURCES} pose_client.cpp vulkan_backend.cpp android_surface.cpp android_surface_swapchain.cpp vulkan_surface.cpp)

target_include_directories(openxr_runtime
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/protocol
)

target_link_libraries(openxr_runtime
  PRIVATE
    axrb_protocol
)

if(ANDROID)
  target_link_libraries(openxr_runtime PRIVATE log GLESv3 EGL android vulkan)
  option(AXRB_BUILD_VULKAN_SMOKE "Build the on-device Vulkan readback regression" OFF)
  if(AXRB_BUILD_VULKAN_SMOKE)
    add_executable(axrb_vulkan_smoke ${PROJECT_SOURCE_DIR}/tests/android/vulkan_android_smoke.cpp vulkan_backend.cpp)
    target_include_directories(axrb_vulkan_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/protocol)
    target_link_libraries(axrb_vulkan_smoke PRIVATE vulkan log)
  endif()
endif()

set_target_properties(openxr_runtime PROPERTIES
  OUTPUT_NAME openxr_runtime
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)
