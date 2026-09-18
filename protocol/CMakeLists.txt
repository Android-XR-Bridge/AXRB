add_library(axrb_protocol STATIC
  gpu_transport.cpp
  image_transport.cpp
  transport_tcp.cpp
  video_transport.cpp
)

target_include_directories(axrb_protocol
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
  target_link_libraries(axrb_protocol PUBLIC ws2_32)
endif()
