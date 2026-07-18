cmake_minimum_required(VERSION 3.16)
project(syn-bar-window-title LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# -Os over -O3: this process spends its life blocked in wl_display_dispatch,
# not computing, so binary size wins over speed. -s strips symbols at link
# time — no separate strip step needed.
set(CMAKE_C_FLAGS_RELEASE "-Os")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

set(PROTOCOL_XML
  /usr/share/wlr-protocols/unstable/wlr-foreign-toplevel-management-unstable-v1.xml)
set(PROTOCOL_HEADER
  ${CMAKE_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h)
set(PROTOCOL_SOURCE
  ${CMAKE_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c)

add_custom_command(OUTPUT ${PROTOCOL_HEADER}
  COMMAND ${WAYLAND_SCANNER} client-header ${PROTOCOL_XML} ${PROTOCOL_HEADER}
  DEPENDS ${PROTOCOL_XML})
add_custom_command(OUTPUT ${PROTOCOL_SOURCE}
  COMMAND ${WAYLAND_SCANNER} private-code ${PROTOCOL_XML} ${PROTOCOL_SOURCE}
  DEPENDS ${PROTOCOL_XML})

add_executable(syn-bar-window-title
  src/main.c
  ${PROTOCOL_SOURCE}
  ${PROTOCOL_HEADER}
)

target_include_directories(syn-bar-window-title PRIVATE
  ${CMAKE_BINARY_DIR}
  ${WAYLAND_CLIENT_INCLUDE_DIRS}
)
target_link_libraries(syn-bar-window-title PRIVATE ${WAYLAND_CLIENT_LIBRARIES})

install(TARGETS syn-bar-window-title RUNTIME DESTINATION lib/syn-os)
