cmake_minimum_required(VERSION 3.16)
project(syn-bar-core LANGUAGES C)

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

# syn_stats.c/.h (per-core CPU + full meminfo readers) reused as-is from
# syn-sysmon-src rather than duplicated — same sibling-source reuse
# syn-connect-src/syn-sysmon-src already make for syn_theme.c.
set(SYSMON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-sysmon-src/src)

add_executable(syn-bar-core
  src/main.c
  src/syn_bar_tone.c
  ${SYSMON_SRC}/syn_stats.c
  ${PROTOCOL_SOURCE}
  ${PROTOCOL_HEADER}
)

target_include_directories(syn-bar-core PRIVATE
  ${CMAKE_BINARY_DIR}
  ${WAYLAND_CLIENT_INCLUDE_DIRS}
  ${SYSMON_SRC}
)
target_link_libraries(syn-bar-core PRIVATE ${WAYLAND_CLIENT_LIBRARIES} m)

install(TARGETS syn-bar-core RUNTIME DESTINATION lib/syn-os)
