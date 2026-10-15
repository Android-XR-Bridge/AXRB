add_library(arun_package STATIC
  src/zip_reader.cc
  src/mapped_file.cc
  src/inflate.cc
  src/dex_info.cc
)
target_include_directories(arun_package PUBLIC include)
target_link_libraries(arun_package PUBLIC arun_diagnostics)
# bcrypt for SHA-256 package hashes. DEFLATE is implemented in inflate.cc:
# Windows ships no raw-DEFLATE decompressor and no host zlib (see that file).
target_link_libraries(arun_package PUBLIC bcrypt.lib)
