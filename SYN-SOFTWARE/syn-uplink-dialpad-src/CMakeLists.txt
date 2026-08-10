cmake_minimum_required(VERSION 3.16)
project(syn-uplink-dialpad LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(XKBCOMMON REQUIRED xkbcommon)
# syn_theme.c's unused ncurses-specific half still pulls in ncursesw at
# link time (same translation unit as the syn_theme_load() half we use).
pkg_check_modules(NCURSES REQUIRED ncursesw)
pkg_get_variable(WAYLAND_SCANNER wayland-scanner wayland_scanner)

set(PROTOCOL_XML /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml)
set(PROTOCOL_HEADER ${CMAKE_BINARY_DIR}/xdg-shell-client-protocol.h)
set(PROTOCOL_SOURCE ${CMAKE_BINARY_DIR}/xdg-shell-protocol.c)

add_custom_command(OUTPUT ${PROTOCOL_HEADER}
  COMMAND ${WAYLAND_SCANNER} client-header ${PROTOCOL_XML} ${PROTOCOL_HEADER}
  DEPENDS ${PROTOCOL_XML})
add_custom_command(OUTPUT ${PROTOCOL_SOURCE}
  COMMAND ${WAYLAND_SCANNER} private-code ${PROTOCOL_XML} ${PROTOCOL_SOURCE}
  DEPENDS ${PROTOCOL_XML})

# syn_bar_notify.c/.h ("ask the bar for a named meaning" + the
# TONE-RAW passthrough this dialpad's own real-time DTMF dialing needs —
# see syn_bar_notify.h) reused as-is from syn-bar-core-src. Only
# syn-bar-core itself links syn_bar_tone.c (the actual paplay
# synthesis).
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

# syn_theme.c/.h are generic SYN_* palette loading, no crypter-specific
# code — reused as-is from syn-crypter-src, same as syn-connect-src does.
set(CRYPTER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-crypter-src/src)

add_executable(syn-uplink-dialpad
  src/main.c
  ${BARCORE_SRC}/syn_bar_notify.c
  ${CRYPTER_SRC}/syn_theme.c
  ${PROTOCOL_SOURCE}
  ${PROTOCOL_HEADER}
)

target_include_directories(syn-uplink-dialpad PRIVATE
  ${CMAKE_BINARY_DIR}
  ${BARCORE_SRC}
  ${CRYPTER_SRC}
  ${WAYLAND_CLIENT_INCLUDE_DIRS}
  ${XKBCOMMON_INCLUDE_DIRS}
  ${NCURSES_INCLUDE_DIRS}
)
target_link_libraries(syn-uplink-dialpad PRIVATE
  ${WAYLAND_CLIENT_LIBRARIES}
  ${XKBCOMMON_LIBRARIES}
  ${NCURSES_LIBRARIES}
  util
)
target_compile_options(syn-uplink-dialpad PRIVATE
  ${WAYLAND_CLIENT_CFLAGS_OTHER}
  ${XKBCOMMON_CFLAGS_OTHER}
  ${NCURSES_CFLAGS_OTHER}
)

install(TARGETS syn-uplink-dialpad RUNTIME DESTINATION lib/syn-os)
