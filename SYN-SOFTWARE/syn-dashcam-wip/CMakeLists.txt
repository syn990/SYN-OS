cmake_minimum_required(VERSION 3.16)
project(syn-dashcam LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# Only the command-line shell so far. README.md has the plan: /proc
# needs no library, and the mark tone is syn_bar_notify.c from
# syn-bar-core-src, reused the way syn-sysmon-src does.
add_executable(syn-dashcam
  src/main.c
)

install(TARGETS syn-dashcam RUNTIME DESTINATION lib/syn-os)
