cmake_minimum_required(VERSION 3.16)
project(syn-bar-notify LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# syn_bar_notify.c/.h are the canonical "ask the bar for a named tone"
# client, reused as-is from syn-bar-core-src — same sibling-source reuse
# pattern every other tool in SYN-SOFTWARE already makes for syn_theme.c.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

add_executable(syn-bar-notify
  src/main.c
  ${BARCORE_SRC}/syn_bar_notify.c
)

target_include_directories(syn-bar-notify PRIVATE ${BARCORE_SRC})

install(TARGETS syn-bar-notify RUNTIME DESTINATION lib/syn-os)
