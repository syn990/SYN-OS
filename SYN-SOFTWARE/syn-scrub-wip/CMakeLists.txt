cmake_minimum_required(VERSION 3.16)
project(syn-scrub LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# Only the command-line shell so far. README.md has the plan; the disk
# level drives cryptsetup, nvme-cli and hdparm as programs rather than
# linking them.
add_executable(syn-scrub
  src/main.c
)

install(TARGETS syn-scrub RUNTIME DESTINATION lib/syn-os)
