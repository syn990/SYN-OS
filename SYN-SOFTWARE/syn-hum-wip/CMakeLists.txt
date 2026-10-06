cmake_minimum_required(VERSION 3.16)
project(syn-hum LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# Only the command-line shell so far. README.md has the plan: no audio
# library here, ever. syn-hum talks to syn-bar-core over its socket
# (syn_bar_notify.c from syn-bar-core-src), and the voice itself lives
# in syn-bar-core.
add_executable(syn-hum
  src/main.c
)

install(TARGETS syn-hum RUNTIME DESTINATION lib/syn-os)
