cmake_minimum_required(VERSION 3.16)
project(syn-audio LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncursesw)
pkg_check_modules(LIBPULSE REQUIRED libpulse)

# syn_theme.c/.h are generic SYN_* palette loading, no crypter-specific
# code — reused as-is from syn-crypter-src rather than duplicated (same
# convention syn-wifi-src follows).
set(CRYPTER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-crypter-src/src)

add_executable(syn-audio
  src/main.c
  src/syn_pulse.c
  src/syn_audio_tui.c
  ${CRYPTER_SRC}/syn_theme.c
)

target_include_directories(syn-audio PRIVATE ${NCURSES_INCLUDE_DIRS} ${LIBPULSE_INCLUDE_DIRS} ${CRYPTER_SRC})
target_link_libraries(syn-audio PRIVATE ${NCURSES_LIBRARIES} ${LIBPULSE_LIBRARIES})
target_compile_options(syn-audio PRIVATE ${NCURSES_CFLAGS_OTHER} ${LIBPULSE_CFLAGS_OTHER})

install(TARGETS syn-audio RUNTIME DESTINATION lib/syn-os)
