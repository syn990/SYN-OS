cmake_minimum_required(VERSION 3.16)
project(syn-node LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(SODIUM REQUIRED libsodium)

# libsynnode: the portable core, libsodium only. Android will build the
# same files into libsynnode.so over JNI, as SYN-CAST-APK does.
add_library(synnode STATIC
  core/sn_util.c
  core/sn_noise.c
  core/sn_identity.c
  core/sn_roster.c
  core/sn_pair.c
  core/sn_wire.c
  core/sn_mesh.c
)
target_include_directories(synnode PUBLIC core ${SODIUM_INCLUDE_DIRS})
target_link_libraries(synnode PUBLIC ${SODIUM_LIBRARIES})
target_compile_options(synnode PRIVATE -Wall -Wextra)

add_executable(syn-noded daemon/main.c daemon/sn_daemon.c)
target_include_directories(syn-noded PRIVATE daemon)
target_link_libraries(syn-noded PRIVATE synnode)
target_compile_options(syn-noded PRIVATE -Wall -Wextra)

add_executable(syn-node cli/main.c)
target_link_libraries(syn-node PRIVATE synnode)
target_compile_options(syn-node PRIVATE -Wall -Wextra)

enable_testing()
foreach(t noise roster pair)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE synnode)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
add_executable(test_dial tests/test_dial.c)
target_link_libraries(test_dial PRIVATE synnode)
add_test(NAME mesh COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_mesh.sh
  $<TARGET_FILE:syn-noded> $<TARGET_FILE:syn-node> $<TARGET_FILE:test_dial>)

install(TARGETS syn-node syn-noded RUNTIME DESTINATION lib/syn-os)
