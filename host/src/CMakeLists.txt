set(AXRB_OPENXR_HEADERS "" CACHE PATH "Directory containing openxr/openxr.h")
if(NOT AXRB_OPENXR_HEADERS)
  set(_local_headers "${PROJECT_SOURCE_DIR}/third_party/openxr-loader-aar/extracted/prefab/modules/headers/include")
  if(EXISTS "${_local_headers}/openxr/openxr.h")
    set(AXRB_OPENXR_HEADERS "${_local_headers}")
  else()
    include(FetchContent)
    FetchContent_Declare(openxr_headers
      GIT_REPOSITORY https://github.com/KhronosGroup/OpenXR-SDK.git
      GIT_TAG release-1.1.60
      GIT_SHALLOW TRUE
      SOURCE_SUBDIR axrb-headers-only)
    FetchContent_MakeAvailable(openxr_headers)
    set(AXRB_OPENXR_HEADERS "${openxr_headers_SOURCE_DIR}/include")
  endif()
endif()

add_executable(axrb-host-bridge
  main.cpp
  openxr_host.cpp
  openxr_session.cpp
  tracking.cpp
  presentation.cpp
  frame_receiver.cpp
  layer_compositor.cpp
  quad_renderer.cpp
  equirect_renderer.cpp
  equirect_compositor.cpp
  panel_swapchain.cpp
  fps_hud.cpp
  splash.cpp
  mirror_window.cpp
)

target_include_directories(axrb-host-bridge
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/protocol
    ${AXRB_OPENXR_HEADERS}
)

target_link_libraries(axrb-host-bridge
  PRIVATE
    axrb_protocol
)

if(UNIX AND NOT APPLE)
  target_link_libraries(axrb-host-bridge PRIVATE dl)
endif()

if(WIN32)
  enable_language(RC)
  configure_file(branding.rc.in "${CMAKE_CURRENT_BINARY_DIR}/branding.rc" @ONLY)
  target_sources(axrb-host-bridge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/branding.rc")
  target_link_libraries(axrb-host-bridge PRIVATE d3d11 d3dcompiler dxgi user32 gdi32 windowscodecs ole32)
endif()
