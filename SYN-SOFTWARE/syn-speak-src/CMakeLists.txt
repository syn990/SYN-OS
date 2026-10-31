cmake_minimum_required(VERSION 3.18)
project(syn-speak LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# sherpa-onnx, the library syn-speak runs Piper on. k2-fsa's prebuilt
# x86_64 release, the same one SYN-CASTER's Makefile vendors (same
# version, same hash): fetched once into the build tree, never built
# here. Its two shared objects ship beside the binary in
# /usr/lib/syn-os/sherpa-onnx, found through an $ORIGIN rpath, so the
# ISO carries no system-wide ONNX runtime. -DSHERPA_DIR=<dir> points at
# an unpacked copy (SYN-CASTER's vendor/sherpa-onnx) for an offline build.
set(SHERPA_VER 1.13.6)
set(SHERPA_TAR sherpa-onnx-v${SHERPA_VER}-linux-x64-shared)
set(SHERPA_SHA256 58cc7d360fdedf9702954e8b585eb400bbee8668484bf25e4461d5d22ba439d8)
set(SHERPA_DIR "" CACHE PATH "An unpacked sherpa-onnx release with include/ and lib/ (fetched when empty)")

if(SHERPA_DIR STREQUAL "")
  set(SHERPA_DIR ${CMAKE_BINARY_DIR}/sherpa-onnx)
  if(NOT EXISTS ${SHERPA_DIR}/lib/libsherpa-onnx-c-api.so)
    set(_tarball ${CMAKE_BINARY_DIR}/${SHERPA_TAR}.tar.bz2)
    message(STATUS "Fetching sherpa-onnx v${SHERPA_VER} (prebuilt, x86_64)")
    file(DOWNLOAD
      https://github.com/k2-fsa/sherpa-onnx/releases/download/v${SHERPA_VER}/${SHERPA_TAR}.tar.bz2
      ${_tarball}
      EXPECTED_HASH SHA256=${SHERPA_SHA256}
      SHOW_PROGRESS STATUS _st)
    list(GET _st 0 _rc)
    if(NOT _rc EQUAL 0)
      message(FATAL_ERROR "sherpa-onnx download failed: ${_st}")
    endif()
    file(ARCHIVE_EXTRACT INPUT ${_tarball} DESTINATION ${CMAKE_BINARY_DIR}/sherpa-unpack
      PATTERNS ${SHERPA_TAR}/include ${SHERPA_TAR}/lib/libsherpa-onnx-c-api.so ${SHERPA_TAR}/lib/libonnxruntime.so)
    file(RENAME ${CMAKE_BINARY_DIR}/sherpa-unpack/${SHERPA_TAR} ${SHERPA_DIR})
    file(REMOVE_RECURSE ${CMAKE_BINARY_DIR}/sherpa-unpack)
    file(REMOVE ${_tarball})
  endif()
endif()

add_executable(syn-speak src/syn-speak.c)
target_include_directories(syn-speak PRIVATE ${SHERPA_DIR}/include)
target_link_directories(syn-speak PRIVATE ${SHERPA_DIR}/lib)
target_link_libraries(syn-speak PRIVATE sherpa-onnx-c-api onnxruntime)
set_target_properties(syn-speak PROPERTIES
  BUILD_RPATH ${SHERPA_DIR}/lib
  INSTALL_RPATH "$ORIGIN/sherpa-onnx"
  BUILD_WITH_INSTALL_RPATH OFF)

add_executable(syn-speak-play src/syn-speak-play.c)

install(TARGETS syn-speak syn-speak-play RUNTIME DESTINATION lib/syn-os)
install(FILES ${SHERPA_DIR}/lib/libsherpa-onnx-c-api.so ${SHERPA_DIR}/lib/libonnxruntime.so
        DESTINATION lib/syn-os/sherpa-onnx)
