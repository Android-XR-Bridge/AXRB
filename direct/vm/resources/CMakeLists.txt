add_library(arun_resources STATIC
  src/res_table.cc
  src/asset_set.cc
)
target_include_directories(arun_resources PUBLIC include)
target_link_libraries(arun_resources PUBLIC arun_manifest arun_diagnostics)
