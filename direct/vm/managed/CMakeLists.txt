add_library(arun_managed STATIC
  src/dex_file.cc
  src/dex_instruction.cc
  src/class.cc
  src/class_linker.cc
  src/heap.cc
  src/monitor.cc
  src/thread.cc
  src/interpreter.cc
  src/runtime.cc
  src/natives_lang.cc
  src/natives_reflect.cc
  src/natives_thread.cc
  src/natives_io.cc
)
target_include_directories(arun_managed PUBLIC include)
# icu.lib: the ICU that ships with Windows 10 1903+ (Unicode character data for
# java.lang.Character). bcrypt.lib: CSPRNG seeds (spec 11).
target_link_libraries(arun_managed PUBLIC arun_diagnostics arun_vfs arun_package icu.lib bcrypt.lib)

# The boot class path (ADR 0004): the in-tree core library and framework,
# compiled with javac + d8 at build time. The runner loads bin/boot/arun-boot.dex.
file(GLOB_RECURSE ARUN_BOOT_JAVA CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/java/core/*.java
  ${CMAKE_CURRENT_SOURCE_DIR}/java/framework/*.java
  ${CMAKE_CURRENT_SOURCE_DIR}/framework-res/*)
set(ARUN_BOOT_DEX ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boot/arun-boot.dex)
add_custom_command(
  OUTPUT ${ARUN_BOOT_DEX} ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boot/arun-framework-res.apk
  COMMAND powershell -NoProfile -ExecutionPolicy Bypass
          -File ${AXRB_VM_DIR}/tools/build-bootclasspath.ps1
          -OutDir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/boot
  DEPENDS ${ARUN_BOOT_JAVA} ${AXRB_VM_DIR}/tools/build-bootclasspath.ps1
          ${AXRB_VM_DIR}/tools/build-framework-res.ps1 ${AXRB_VM_DIR}/tools/toolchain.json
  COMMENT "Building the framework resources (aapt2) and the boot class path (javac + d8)"
  VERBATIM)
add_custom_target(arun_boot_dex DEPENDS ${ARUN_BOOT_DEX})
