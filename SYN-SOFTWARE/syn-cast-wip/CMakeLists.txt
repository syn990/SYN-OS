cmake_minimum_required(VERSION 3.16)
project(syn-cast LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")
add_compile_options(-Wall -Wextra -D_GNU_SOURCE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(DEPS REQUIRED
  wayland-client wayland-cursor xkbcommon fcft pixman-1
  libpulse-simple opus rnnoise libturbojpeg jansson openssl)
# syn_theme.c's unused ncurses half still pulls in ncursesw at link time,
# the same as syn-uplink-dialpad.
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
# xdg-decoration: ask the compositor for its own frame (labwc draws none
# for a toplevel that doesn't ask)
set(DECO_XML /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml)
set(DECO_HEADER ${CMAKE_BINARY_DIR}/xdg-decoration-client-protocol.h)
set(DECO_SOURCE ${CMAKE_BINARY_DIR}/xdg-decoration-protocol.c)
add_custom_command(OUTPUT ${DECO_HEADER}
  COMMAND ${WAYLAND_SCANNER} client-header ${DECO_XML} ${DECO_HEADER}
  DEPENDS ${DECO_XML})
add_custom_command(OUTPUT ${DECO_SOURCE}
  COMMAND ${WAYLAND_SCANNER} private-code ${DECO_XML} ${DECO_SOURCE}
  DEPENDS ${DECO_XML})

# Tones are asked of syn-bar-core by name (syn_bar_notify.c), and colours
# come from the live theme (syn_theme.c), the same files the other SYN-OS
# tools reuse.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)
set(CRYPTER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-crypter-src/src)

set(CORE_SOURCES
  src/util.c src/zproto.c src/jwt.c src/ws.c src/config.c src/history.c
  src/audio.c src/stretch.c src/image.c src/app.c src/ctl.c
  ${BARCORE_SRC}/syn_bar_notify.c)

add_executable(syn-cast
  src/main.c src/ui.c ${CORE_SOURCES}
  ${CRYPTER_SRC}/syn_theme.c
  ${PROTOCOL_SOURCE} ${PROTOCOL_HEADER} ${DECO_SOURCE} ${DECO_HEADER})
target_include_directories(syn-cast PRIVATE src ${CMAKE_BINARY_DIR} ${BARCORE_SRC} ${CRYPTER_SRC}
  ${DEPS_INCLUDE_DIRS} ${NCURSES_INCLUDE_DIRS})
target_link_libraries(syn-cast PRIVATE ${DEPS_LIBRARIES} ${NCURSES_LIBRARIES} m pthread)
target_compile_options(syn-cast PRIVATE ${DEPS_CFLAGS_OTHER})

# tests: `cmake --build build && ctest --test-dir build`
enable_testing()
add_executable(proto_test tests/proto_test.c src/util.c src/zproto.c src/history.c src/stretch.c src/jwt.c)
target_include_directories(proto_test PRIVATE src ${DEPS_INCLUDE_DIRS})
target_link_libraries(proto_test PRIVATE ${DEPS_LIBRARIES} m)
add_test(NAME proto COMMAND proto_test)
add_test(NAME e2e COMMAND python3 -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e.py $<TARGET_FILE:syn-cast>)
set_tests_properties(e2e PROPERTIES TIMEOUT 180)

install(TARGETS syn-cast RUNTIME DESTINATION bin)
install(FILES syn-cast.desktop DESTINATION share/applications)
