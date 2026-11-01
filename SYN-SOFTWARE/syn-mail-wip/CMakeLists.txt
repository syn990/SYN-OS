cmake_minimum_required(VERSION 3.16)
project(syn-mail LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")
add_compile_options(-Wall -Wextra -D_GNU_SOURCE)

find_package(PkgConfig REQUIRED)
find_package(OpenSSL REQUIRED)
pkg_check_modules(DEPS REQUIRED IMPORTED_TARGET jansson libsecret-1 libcurl)

# Slice 1: the command-line core. README.md has the plan; the window (slice 2) and the APK (slice 3) follow.
add_executable(syn-mail
  src/main.c
  src/config.c
  src/imap.c
  src/mime.c
  src/msg.c
  src/net.c
  src/oauth.c
  src/secret.c
  src/smtp.c
  src/util.c
)
target_link_libraries(syn-mail PRIVATE OpenSSL::SSL OpenSSL::Crypto PkgConfig::DEPS)

install(TARGETS syn-mail RUNTIME DESTINATION bin)
