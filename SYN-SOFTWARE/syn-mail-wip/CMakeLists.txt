cmake_minimum_required(VERSION 3.16)
project(syn-mail LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")
add_compile_options(-Wall -Wextra -D_GNU_SOURCE)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET jansson libsecret-1 libcurl)
# Slice 2: the window, a native Wayland client drawn the way SYN-CAST draws (pixman, fcft, the SYN-OS theme).
pkg_check_modules(UI REQUIRED IMPORTED_TARGET wayland-client wayland-cursor xkbcommon fcft pixman-1 libpng ncursesw)
pkg_get_variable(WL_PROTOCOLS wayland-protocols pkgdatadir)
pkg_get_variable(WL_SCANNER wayland-scanner wayland_scanner)
if(NOT WL_PROTOCOLS)
  set(WL_PROTOCOLS /usr/share/wayland-protocols)
endif()
if(NOT WL_SCANNER)
  set(WL_SCANNER wayland-scanner)
endif()
set(PROTO_SRCS)
foreach(proto "stable/xdg-shell/xdg-shell.xml" "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
  get_filename_component(base ${proto} NAME_WE)
  string(REPLACE "-unstable-v1" "" short ${base})
  set(xml ${WL_PROTOCOLS}/${proto})
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${short}-client-protocol.h
    COMMAND ${WL_SCANNER} client-header ${xml} ${CMAKE_BINARY_DIR}/${short}-client-protocol.h DEPENDS ${xml})
  add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/${short}-protocol.c
    COMMAND ${WL_SCANNER} private-code ${xml} ${CMAKE_BINARY_DIR}/${short}-protocol.c DEPENDS ${xml})
  list(APPEND PROTO_SRCS ${CMAKE_BINARY_DIR}/${short}-client-protocol.h ${CMAKE_BINARY_DIR}/${short}-protocol.c)
endforeach()
# the theme reader is shared with SYN-CRYPTER, the same file SYN-CAST builds in
set(CRYPTER_SRC ${CMAKE_SOURCE_DIR}/../syn-crypter-src/src)

# Slice 1: the command-line core; slice 2: the window. README.md has the plan; the APK (slice 3) follows.
add_executable(syn-mail
  src/main.c
  src/box.c
  src/worker.c
  src/ui.c
  ${CRYPTER_SRC}/syn_theme.c
  ${PROTO_SRCS}
  src/config.c
  src/imap.c
  src/mime.c
  src/html.c
  src/msg.c
  src/letter.c
  src/net.c
  src/oauth.c
  src/secret.c
  src/smtp.c
  src/util.c
)
target_include_directories(syn-mail PRIVATE src ${CMAKE_BINARY_DIR} ${CRYPTER_SRC})
target_link_libraries(syn-mail PRIVATE OpenSSL::SSL OpenSSL::Crypto PkgConfig::DEPS PkgConfig::UI pthread m)

install(TARGETS syn-mail RUNTIME DESTINATION bin)
