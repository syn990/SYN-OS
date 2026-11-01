# libsyncast.so: the JNI glue over a static libopus (xiph's release
# tarball, checksum-verified by build.sh), one per Android ABI.
cmake_minimum_required(VERSION 3.16)
project(syncast C)

set(OPUS_SRC "" CACHE PATH "unpacked libopus release")
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
# the neural extras (DRED, OSCE) add megabytes of weights and nothing the
# Channel API carries
set(OPUS_DRED OFF CACHE BOOL "" FORCE)
set(OPUS_OSCE OFF CACHE BOOL "" FORCE)
add_subdirectory(${OPUS_SRC} opus EXCLUDE_FROM_ALL)

add_library(syncast SHARED syncast_opus.c)
target_link_libraries(syncast PRIVATE opus)
target_link_options(syncast PRIVATE -Wl,--gc-sections -Wl,-z,max-page-size=16384)
