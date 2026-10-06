cmake_minimum_required(VERSION 3.16)
project(syn-boot LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# Only the command-line shell so far. README.md has the plan: /proc and
# runit's supervise files need no library, and the SVG's colors come
# from syn_theme.c in syn-crypter-src.
add_executable(syn-boot
  src/main.c
)

install(TARGETS syn-boot RUNTIME DESTINATION lib/syn-os)
