add_library(arun_manifest STATIC
  src/axml.cc
  src/manifest.cc
  src/res_string_pool.cc
)
target_include_directories(arun_manifest PUBLIC include)
target_link_libraries(arun_manifest PUBLIC arun_diagnostics)
