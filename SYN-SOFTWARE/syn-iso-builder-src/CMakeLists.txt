cmake_minimum_required(VERSION 3.16)
project(syn-iso-builder LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncursesw)

# syn_bar_notify.c/.h ("ask the bar for a named tone") reused as-is from
# syn-bar-core-src — only syn-bar-core itself links syn_bar_tone.c (the
# actual paplay synthesis), see syn_bar_notify.h.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

add_executable(syn-iso-builder
  src/main.c
  src/syn_theme.c
  src/syn_tui_basic.c
  src/syn_tui_scrollmenu.c
  src/syn_tui_dirpicker.c
  src/syn_tui_modal.c
  src/syn_tui_buildlog.c
  src/syn_tui_dashboard.c
  src/syn_git_history.c
  src/syn_git_extract.c
  src/syn_build_paths.c
  src/syn_repo_locate.c
  src/syn_mount_cleanup.c
  src/syn_software_build.c
  src/syn_build_runner.c
  ${BARCORE_SRC}/syn_bar_notify.c
)

target_include_directories(syn-iso-builder PRIVATE ${NCURSES_INCLUDE_DIRS} ${BARCORE_SRC})
target_link_libraries(syn-iso-builder PRIVATE ${NCURSES_LIBRARIES} util)

install(TARGETS syn-iso-builder RUNTIME DESTINATION lib/syn-os)
