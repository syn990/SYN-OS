cmake_minimum_required(VERSION 3.16)
project(syn-shell LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
# libarchive reads and writes every archive format the browser handles
# (see src/Archive.h); its pkg-config file is what both streams install.
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBARCHIVE REQUIRED IMPORTED_TARGET libarchive)
# libvterm is the terminal area's emulator (see src/TermView.h).
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)

# syn_bar_notify.c/.h reused as-is from syn-bar-core-src — same
# sibling-source reuse syn-connect-src/syn-crypter-src already make,
# just needs LANGUAGES C added above since this project was CXX-only
# until now (FileOps.cpp calls into syn_bar_notify_meaning() via an
# extern "C" declaration — see FileOps.cpp). Only syn-bar-core itself
# links syn_bar_tone.c (the actual paplay synthesis), see
# syn_bar_notify.h.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../syn-bar-core-src/src)

add_executable(syn-shell
  src/main.cpp
  src/MainWindow.cpp
  src/MainWindow.h
  src/MainWindowArchive.cpp
  src/MainWindowFiles.cpp
  src/MainWindowShell.cpp
  src/FileOps.cpp
  src/FileOps.h
  src/FileJobs.cpp
  src/FileJobs.h
  src/Jobs.cpp
  src/Jobs.h
  src/JobsWindow.cpp
  src/JobsWindow.h
  src/Archive.cpp
  src/Archive.h
  src/ArchiveModel.cpp
  src/ArchiveModel.h
  src/FileSortProxy.cpp
  src/FileSortProxy.h
  src/Preview.cpp
  src/PtySession.cpp
  src/PtySession.h
  src/Preview.h
  src/RowDelegate.cpp
  src/RowDelegate.h
  src/SynIcons.cpp
  src/SynIcons.h
  src/TermView.cpp
  src/TermView.h
  src/Zoom.h
  ${BARCORE_SRC}/syn_bar_notify.c
)

target_include_directories(syn-shell PRIVATE ${BARCORE_SRC})
target_link_libraries(syn-shell PRIVATE Qt6::Widgets Qt6::Network PkgConfig::LIBARCHIVE PkgConfig::VTERM)

install(TARGETS syn-shell RUNTIME DESTINATION bin)
install(FILES resources/syn-shell.desktop
  DESTINATION share/applications)
# Its old name, so menus, keybinds and scripts written before the rename
# (every existing ~/.config copy of the dotfiles) still open it.
install(CODE "file(CREATE_LINK syn-shell \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/syn-filemanager\" SYMBOLIC)")
