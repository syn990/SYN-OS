cmake_minimum_required(VERSION 3.16)
project(syn-bar-vpn LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
# -Os over -O3: this runs once per 5s tick and exits, not a hot loop —
# binary size wins over speed. -s strips symbols at link time.
set(CMAKE_C_FLAGS_RELEASE "-Os")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

add_executable(syn-bar-vpn src/main.c)

install(TARGETS syn-bar-vpn RUNTIME DESTINATION lib/syn-os)
