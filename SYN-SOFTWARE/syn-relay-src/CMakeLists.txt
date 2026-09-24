cmake_minimum_required(VERSION 3.16)
project(syn-relay LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-Os")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(AVCODEC        REQUIRED libavcodec)
pkg_check_modules(AVFORMAT       REQUIRED libavformat)
pkg_check_modules(AVUTIL         REQUIRED libavutil)
pkg_check_modules(SWSCALE        REQUIRED libswscale)
# sdl2 (SDL2 proper), or sdl2-compat, SDL2's API on SDL3 (SYN-OS on runit)
pkg_search_module(SDL2          REQUIRED sdl2 sdl2-compat)

# syn_bar_notify.c/.h ("ask the bar for a named tone") reused as-is
# from syn-bar-core-src — only syn-bar-core itself links syn_bar_tone.c
# (the actual paplay synthesis), see syn_bar_notify.h. Same sibling-
# source reuse pattern syn-connect-src/syn-crypter-src make for other
# files.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

add_executable(syn-relay
  src/main.c
  src/cmd_stats_client.c
  src/cmd_stats_server.c
  src/cmd_watch.c
  src/cmd_host.c
  src/cmd_stream_app.c
  src/syn_dialpad_prompt.c
  src/syn_relay_conn.c
  src/syn_ssh_resolve.c
  src/syn_node_state.c
  src/syn_json_extract.c
  src/syn_local_stats.c
  src/syn_stats.c
  src/syn_disk.c
  src/syn_gpu.c
  src/syn_sunshine.c
  src/syn_apps.c
  src/syn_agent_state.c
  ${BARCORE_SRC}/syn_bar_notify.c
  protocol/ext-image-capture-source-v1-protocol.c
  protocol/ext-image-copy-capture-v1-protocol.c
  protocol/ext-foreign-toplevel-list-v1-protocol.c
  protocol/wlr-virtual-pointer-unstable-v1-protocol.c
)

target_include_directories(syn-relay PRIVATE
  src
  protocol
  ${BARCORE_SRC}
  ${WAYLAND_CLIENT_INCLUDE_DIRS}
  ${AVCODEC_INCLUDE_DIRS}
  ${AVFORMAT_INCLUDE_DIRS}
  ${AVUTIL_INCLUDE_DIRS}
  ${SWSCALE_INCLUDE_DIRS}
  ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(syn-relay PRIVATE
  ${WAYLAND_CLIENT_LIBRARIES}
  ${AVCODEC_LIBRARIES}
  ${AVFORMAT_LIBRARIES}
  ${AVUTIL_LIBRARIES}
  ${SWSCALE_LIBRARIES}
  ${SDL2_LIBRARIES}
)
target_compile_options(syn-relay PRIVATE
  ${WAYLAND_CLIENT_CFLAGS_OTHER}
  ${AVCODEC_CFLAGS_OTHER}
  ${AVFORMAT_CFLAGS_OTHER}
  ${AVUTIL_CFLAGS_OTHER}
  ${SWSCALE_CFLAGS_OTHER}
  ${SDL2_CFLAGS_OTHER}
)

install(TARGETS syn-relay RUNTIME DESTINATION lib/syn-os)
