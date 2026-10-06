cmake_minimum_required(VERSION 3.16)
project(syn-autopsy LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# Only the command-line shell so far. Libraries come in with the code
# that needs them (README.md has the plan): OpenSSL's libcrypto for
# SHA-256, the same openssl module syn-crypter-src already links.
add_executable(syn-autopsy
  src/main.c
)

install(TARGETS syn-autopsy RUNTIME DESTINATION lib/syn-os)
