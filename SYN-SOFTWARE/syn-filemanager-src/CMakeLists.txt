cmake_minimum_required(VERSION 3.16)
project(syn-filemanager LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# syn_bar_notify.c/.h reused as-is from syn-bar-core-src — same
# sibling-source reuse syn-connect-src/syn-crypter-src already make,
# just needs LANGUAGES C added above since this project was CXX-only
# until now (FileOps.cpp calls into syn_bar_notify_meaning() via an
# extern "C" declaration — see FileOps.cpp). Only syn-bar-core itself
# links syn_bar_tone.c (the actual paplay synthesis), see
# syn_bar_notify.h.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

add_executable(syn-filemanager
  src/main.cpp
  src/MainWindow.cpp
  src/MainWindow.h
  src/FileOps.cpp
  src/FileOps.h
  ${BARCORE_SRC}/syn_bar_notify.c
)

target_include_directories(syn-filemanager PRIVATE ${BARCORE_SRC})
target_link_libraries(syn-filemanager PRIVATE Qt6::Widgets)

install(TARGETS syn-filemanager RUNTIME DESTINATION bin)
install(FILES resources/syn-filemanager.desktop
  DESTINATION share/applications)
