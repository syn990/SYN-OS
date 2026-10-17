cmake_minimum_required(VERSION 3.16)
project(syn-hum LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# No audio library here, ever: syn-hum sends syn-bar-core its levels over
# the bar's socket and syn-bar-core plays them (syn_bar_hum.c). From
# syn-bar-core-src it takes the styles' words (syn_hum_styles.h, for
# `styles` and `test`) and syn_bar_notify.c for the on and off tones.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

# syn_stats.c (CPU) and the log readers reused as-is from syn-sysmon-src,
# the same sibling-source reuse syn-bar-core-src makes
set(SYSMON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-sysmon-src/src)

find_package(PkgConfig REQUIRED)
# Log errors come from the systemd journal where there is one (Arch), and
# from svlogd's and syslogd's files otherwise (SYN-OS on runit), picked
# the way syn-sysmon picks them
pkg_check_modules(SDJOURNAL libsystemd)
if(SDJOURNAL_FOUND)
  set(JOURNAL_SRC ${SYSMON_SRC}/syn_journal.c)
else()
  set(JOURNAL_SRC ${SYSMON_SRC}/syn_journal_files.c)
endif()

add_executable(syn-hum
  src/main.c
  src/syn_hum_sample.c
  src/syn_hum_map.c
  src/syn_hum_conf.c
  src/syn_hum_send.c
  ${SYSMON_SRC}/syn_stats.c
  ${JOURNAL_SRC}
  ${BARCORE_SRC}/syn_bar_notify.c
)

target_include_directories(syn-hum PRIVATE ${SDJOURNAL_INCLUDE_DIRS} ${SYSMON_SRC} ${BARCORE_SRC})
target_link_libraries(syn-hum PRIVATE ${SDJOURNAL_LIBRARIES} m)
target_compile_options(syn-hum PRIVATE ${SDJOURNAL_CFLAGS_OTHER})

install(TARGETS syn-hum RUNTIME DESTINATION lib/syn-os)
