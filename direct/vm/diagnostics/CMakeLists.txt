add_library(arun_diagnostics STATIC
  src/status.cc
  src/log.cc
)
target_include_directories(arun_diagnostics PUBLIC include)
