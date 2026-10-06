cmake_minimum_required(VERSION 3.16)
project(syn-fsn LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# Only the command-line shell so far. README.md has the plan: the layout
# needs no library; the renderer brings SDL2 (sdl2 or sdl2-compat, as
# syn-relay-src finds it), SDL2_ttf and libepoxy, with its matrix math
# written here rather than a library.
add_executable(syn-fsn
  src/main.c
)

install(TARGETS syn-fsn RUNTIME DESTINATION lib/syn-os)
