add_library(arun_framework_host STATIC
  src/framework_host.cc
  src/render.cc
  src/resources_host.cc
  src/graphics_natives.cc
)
target_include_directories(arun_framework_host PUBLIC include PRIVATE src)
# d2d1/dwrite: rendering and text. windowscodecs/ole32: image decoding/encoding (WIC).
# gdi32/user32: the window.
target_link_libraries(arun_framework_host PUBLIC arun_managed arun_package arun_resources
  PRIVATE user32.lib gdi32.lib ole32.lib windowscodecs.lib d2d1.lib dwrite.lib)
