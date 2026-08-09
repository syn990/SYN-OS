cmake_minimum_required(VERSION 3.16)
project(syn-sysmon LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncursesw)
pkg_check_modules(SDJOURNAL REQUIRED libsystemd)

# syn_theme.c/.h are generic SYN_* palette loading, no crypter-specific
# code — reused as-is from syn-crypter-src rather than duplicated (same
# reuse syn-connect-src already makes).
set(CRYPTER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-crypter-src/src)

# syn_bar_tone.c/.h + syn_tone_vocab.h reused as-is from syn-bar-core-src
# — same sibling-source reuse pattern as CRYPTER_SRC above.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

add_executable(syn-sysmon
  src/main.c
  src/syn_stats.c
  src/syn_journal.c
  src/syn_sysmon_tui.c
  src/syn_bar_client.c
  ${CRYPTER_SRC}/syn_theme.c
  ${BARCORE_SRC}/syn_bar_tone.c
)

target_include_directories(syn-sysmon PRIVATE ${NCURSES_INCLUDE_DIRS} ${SDJOURNAL_INCLUDE_DIRS} ${CRYPTER_SRC} ${BARCORE_SRC})
target_link_libraries(syn-sysmon PRIVATE ${NCURSES_LIBRARIES} ${SDJOURNAL_LIBRARIES} m)
target_compile_options(syn-sysmon PRIVATE ${NCURSES_CFLAGS_OTHER} ${SDJOURNAL_CFLAGS_OTHER})

install(TARGETS syn-sysmon RUNTIME DESTINATION lib/syn-os)
