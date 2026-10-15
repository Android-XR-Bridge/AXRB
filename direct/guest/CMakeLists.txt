# ARM64 Android binaries that run inside the guest. They are built with the
# NDK's clang and placed next to axrb-direct.exe under guest/.

set(GUEST_OUT "${CMAKE_BINARY_DIR}/guest")
set(GUEST_CLANG "${NDK_BIN}/clang.exe")
set(GUEST_CLANGXX "${NDK_BIN}/clang++.exe")
set(GUEST_TARGET --target=aarch64-linux-android29)
file(MAKE_DIRECTORY "${GUEST_OUT}")

set(_guest_outputs)
# axrb_guest(<output> KIND exe|lib SOURCES ... [FLAGS ...])
function(axrb_guest name)
  cmake_parse_arguments(G "" "KIND" "SOURCES;FLAGS;DEPENDS" ${ARGN})
  set(out "${GUEST_OUT}/${name}")
  if(G_KIND STREQUAL "lib")
    get_filename_component(soname "${name}" NAME)
    set(kind_flags -fPIC -shared -Wl,-soname,${soname})
  else()
    set(kind_flags -fPIE -pie)
  endif()
  set(compiler "${GUEST_CLANGXX}")
  list(GET G_SOURCES 0 first_source)
  if(first_source MATCHES "\.c$")
    set(compiler "${GUEST_CLANG}")
  endif()
  add_custom_command(OUTPUT "${out}"
    COMMAND "${compiler}" ${GUEST_TARGET} -O2 -Wall ${kind_flags} ${G_SOURCES} ${G_FLAGS} -o "${out}"
    DEPENDS ${G_SOURCES} ${G_DEPENDS} "${CMAKE_CURRENT_SOURCE_DIR}/axrb_guest.h" VERBATIM)
  set(_guest_outputs ${_guest_outputs} "${out}" PARENT_SCOPE)
endfunction()

axrb_guest(hello KIND exe SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/hello.c" FLAGS -static-libstdc++)
axrb_guest(selftest KIND exe SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/selftest.cpp" FLAGS -static-libstdc++)

axrb_guest(axrb_app KIND exe SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/axrb_app.c")

# NDK libraries provided in place of the system ones (guest /apex/com.axrb.direct/lib64).
file(MAKE_DIRECTORY "${GUEST_OUT}/lib64")
set(P "${CMAKE_CURRENT_SOURCE_DIR}/proxy")
axrb_guest(lib64/libandroid.so KIND lib SOURCES "${P}/libandroid.c" "${P}/native_activity.c")
axrb_guest(lib64/libEGL.so KIND lib SOURCES "${P}/libEGL.c")
axrb_guest(lib64/libGLESv1_CM.so KIND lib SOURCES "${P}/libGLES.c")
axrb_guest(lib64/libGLESv2.so KIND lib SOURCES "${P}/libGLES.c")
axrb_guest(lib64/libGLESv3.so KIND lib SOURCES "${P}/libGLES.c")
axrb_guest(lib64/libOpenSLES.so KIND lib SOURCES "${P}/libOpenSLES.c"
  DEPENDS "${P}/opensles_output.inc" FLAGS -laaudio -lm)
axrb_guest(lib64/libaaudio.so KIND lib SOURCES "${P}/libaaudio.c")
axrb_guest(lib64/libvulkan.so KIND lib SOURCES "${P}/libvulkan.cpp"
  FLAGS -std=c++17 -fno-exceptions -fno-rtti -nostdlib++ -fvisibility=default)

add_custom_target(axrb_guest ALL DEPENDS ${_guest_outputs})
