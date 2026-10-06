cmake_minimum_required(VERSION 3.16)
project(syn-lag LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# Only the command-line shell so far. README.md has the plan:
# wayland-client and xkbcommon (as syn-uplink-dialpad-src links them),
# the capture protocols' generated code from syn-relay-src/protocol/,
# and virtual-keyboard-unstable-v1 generated into protocol/ here.
add_executable(syn-lag
  src/main.c
)

install(TARGETS syn-lag RUNTIME DESTINATION lib/syn-os)
