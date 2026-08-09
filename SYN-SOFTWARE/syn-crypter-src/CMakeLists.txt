cmake_minimum_required(VERSION 3.16)
project(syn-crypter LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENSSL REQUIRED openssl)
pkg_check_modules(NCURSES REQUIRED ncursesw)

# syn_bar_tone.c/.h + syn_tone_vocab.h reused as-is from syn-bar-core-src
# — same sibling-source reuse pattern syn-connect-src/syn-uplink-dialpad-src
# already make for other files.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

add_executable(syn-crypter
  src/main.c
  src/syn_crypt_util.c
  src/syn_crypt_aes.c
  src/syn_crypt_blowfish.c
  src/syn_crypt_rsa.c
  src/syn_crypt_redshirt.c
  src/syn_theme.c
  src/syn_tui.c
  ${BARCORE_SRC}/syn_bar_tone.c
)

target_include_directories(syn-crypter PRIVATE ${OPENSSL_INCLUDE_DIRS} ${NCURSES_INCLUDE_DIRS} ${BARCORE_SRC})
target_link_libraries(syn-crypter PRIVATE ${OPENSSL_LIBRARIES} ${NCURSES_LIBRARIES} m)
target_compile_options(syn-crypter PRIVATE ${OPENSSL_CFLAGS_OTHER} ${NCURSES_CFLAGS_OTHER})

install(TARGETS syn-crypter RUNTIME DESTINATION lib/syn-os)
