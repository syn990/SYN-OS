cmake_minimum_required(VERSION 3.16)
project(syn-wifi LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSES REQUIRED ncursesw)
pkg_check_modules(SDBUS REQUIRED libsystemd)

# syn_theme.c/.h are generic SYN_* palette loading, no crypter-specific
# code — reused as-is from syn-crypter-src rather than duplicated.
set(CRYPTER_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-crypter-src/src)

add_executable(syn-wifi
  src/main.c
  src/syn_iwd.c
  src/syn_wifi_tui.c
  ${CRYPTER_SRC}/syn_theme.c
)

target_include_directories(syn-wifi PRIVATE ${NCURSES_INCLUDE_DIRS} ${SDBUS_INCLUDE_DIRS} ${CRYPTER_SRC})
target_link_libraries(syn-wifi PRIVATE ${NCURSES_LIBRARIES} ${SDBUS_LIBRARIES})
target_compile_options(syn-wifi PRIVATE ${NCURSES_CFLAGS_OTHER} ${SDBUS_CFLAGS_OTHER})

install(TARGETS syn-wifi RUNTIME DESTINATION lib/syn-os)
