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
  # The host loads OpenXR at runtime, so its development output needs the same
  # pinned Khronos loader that release preparation puts beside the packaged host.
  set(_openxr_loader_archive "${PROJECT_SOURCE_DIR}/out/distribution/OpenXR.Loader.1.1.60.nupkg")
  file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/out/distribution")
  file(DOWNLOAD
    "https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-1.1.60/OpenXR.Loader.1.1.60.nupkg"
    "${_openxr_loader_archive}"
    EXPECTED_HASH SHA256=93c800cfe3269a19683fe6fef51236c16a164c641a6903eff84c6b2816afe556
    TLS_VERIFY ON
    STATUS _openxr_loader_download)
  list(GET _openxr_loader_download 0 _openxr_loader_code)
  if(NOT _openxr_loader_code EQUAL 0)
    message(FATAL_ERROR "Could not download the Khronos OpenXR loader: ${_openxr_loader_download}")
  endif()
  set(_openxr_loader_dir "${CMAKE_CURRENT_BINARY_DIR}/openxr-loader")
  set(_openxr_loader_dll "${_openxr_loader_dir}/native/x64/release/bin/openxr_loader.dll")
  if(NOT EXISTS "${_openxr_loader_dll}")
    file(MAKE_DIRECTORY "${_openxr_loader_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_openxr_loader_archive}" DESTINATION "${_openxr_loader_dir}")
  endif()
  if(NOT EXISTS "${_openxr_loader_dll}")
    message(FATAL_ERROR "Khronos OpenXR loader package does not contain the Windows x64 DLL")
  endif()
  add_custom_command(TARGET axrb-host-bridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_openxr_loader_dll}" "$<TARGET_FILE_DIR:axrb-host-bridge>/openxr_loader.dll"
    VERBATIM)
  enable_language(RC)
  configure_file(branding.rc.in "${CMAKE_CURRENT_BINARY_DIR}/branding.rc" @ONLY)
  target_sources(axrb-host-bridge PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/branding.rc")
  target_link_libraries(axrb-host-bridge PRIVATE d3d11 d3dcompiler dxgi user32 gdi32 windowscodecs ole32)
endif()
