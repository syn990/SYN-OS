cmake_minimum_required(VERSION 3.16)
project(syn-bar LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# -Os over -O3: every flag here is a socket round-trip immediately
# followed by exit, not computing — binary size wins over speed. -s
# strips symbols at link time.
set(CMAKE_C_FLAGS_RELEASE "-Os")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

add_executable(syn-bar src/main.c)

install(TARGETS syn-bar RUNTIME DESTINATION lib/syn-os)
