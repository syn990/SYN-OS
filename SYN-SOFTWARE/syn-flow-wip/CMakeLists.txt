cmake_minimum_required(VERSION 3.16)
project(syn-flow LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# Only the command-line shell so far. Libraries come in with the code
# that needs them (README.md has the plan): syn_theme.c from
# syn-crypter-src for the palette, and Graphviz's dot at run time.
add_executable(syn-flow
  src/main.c
)

install(TARGETS syn-flow RUNTIME DESTINATION lib/syn-os)
