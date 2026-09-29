# Optional host-only module. No Android/game changes are required.
target_sources(axrb-host-bridge PRIVATE performance_overlay.cpp mirror_hud.cpp)
target_include_directories(axrb-host-bridge PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(axrb-host-bridge PRIVATE AXRB_ENABLE_PERFORMANCE_OVERLAY=1)
