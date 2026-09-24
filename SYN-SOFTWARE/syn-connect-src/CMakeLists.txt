cmake_minimum_required(VERSION 3.16)
project(syn-connect LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncursesw)
# sd-bus from systemd (Arch), elogind, or basu, the standalone copy that
# SYN-OS on runit uses. Same API; see syn_sd_bus.h for the header.
pkg_search_module(SDBUS REQUIRED libsystemd libelogind basu)

# syn_theme.c/.h are generic SYN_* palette loading, no crypter-specific
# code — reused as-is from syn-crypter-src rather than duplicated.
set(CRYPTER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-crypter-src/src)

# syn_bar_notify.c/.h ("ask the bar for a named tone") reused as-is from
# syn-bar-core-src — same sibling-source reuse pattern as CRYPTER_SRC
# above. syn_bar_tone.c/.h (the actual paplay synthesis) is deliberately
# NOT linked here — only syn-bar-core itself is allowed to touch
# PulseAudio, see syn_bar_notify.h.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

# Bluetooth support talks to BlueZ purely over the system D-Bus (see
# syn_bluez.c) — same as syn_iwd.c does for iwd. No BlueZ dev package or
# libbluetooth needed at build time; only libsystemd (already linked for
# iwd) for sd-bus. bluez itself is a runtime dependency (bluetoothd must
# be running), declared in PKGBUILD, not here.

add_executable(syn-connect
  src/main.c
  src/syn_iwd.c
  src/syn_bluez.c
  src/syn_eth.c
  src/syn_vpn.c
  src/syn_tui.c
  ${CRYPTER_SRC}/syn_theme.c
  ${BARCORE_SRC}/syn_bar_notify.c
)

target_include_directories(syn-connect PRIVATE ${NCURSES_INCLUDE_DIRS} ${SDBUS_INCLUDE_DIRS} ${CRYPTER_SRC} ${BARCORE_SRC})
target_link_libraries(syn-connect PRIVATE ${NCURSES_LIBRARIES} ${SDBUS_LIBRARIES})
target_compile_options(syn-connect PRIVATE ${NCURSES_CFLAGS_OTHER} ${SDBUS_CFLAGS_OTHER})

install(TARGETS syn-connect RUNTIME DESTINATION lib/syn-os)
