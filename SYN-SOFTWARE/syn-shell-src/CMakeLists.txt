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
if (WIN32)
  # MSYS2's vterm.pc has no prefix= line, so pkgconf can't move its
  # folders to where MSYS2 sits and hands over "/ucrt64/include", which
  # isn't a Windows path. Found directly instead, under the same name.
  find_path(VTERM_INCLUDE_DIR vterm.h REQUIRED)
  find_library(VTERM_LIBRARY vterm REQUIRED)
  add_library(PkgConfig::VTERM UNKNOWN IMPORTED)
  set_target_properties(PkgConfig::VTERM PROPERTIES
    IMPORTED_LOCATION "${VTERM_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${VTERM_INCLUDE_DIR}")
else()
  pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
endif()
# KDE's syntax highlighting engine colours code previews. Optional: the
# previews stay plain without it.
find_package(KF6SyntaxHighlighting QUIET)

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
  src/MainWindowMedia.cpp
  src/MainWindowRename.cpp
  src/MainWindowShell.cpp
  src/MainWindowSort.cpp
  src/MainWindowTabs.cpp
  src/MainWindowTools.cpp
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
  src/GitStatus.cpp
  src/GitStatus.h
  src/FileSortProxy.h
  src/Preview.cpp
  src/PtySession.h
  src/Preview.h
  src/RowDelegate.cpp
  src/RowDelegate.h
  src/SortOrder.cpp
  src/SortOrder.h
  src/SynIcons.cpp
  src/SynIcons.h
  src/TermView.cpp
  src/TermView.h
  src/Zoom.h
)

# What's the operating system's own: on Linux the shell's pty and signals,
# and syn-bar-core for sounds; on Windows a pseudoconsole (ConPTY) and
# named pipes, the Recycle Bin, file associations and the SYN theme from
# SYN-WIN (the *_win.cpp files and Win.cpp). The rest, and the
# small Windows parts inside it (#ifdef Q_OS_WIN), is one source.
if (WIN32)
  target_sources(syn-shell PRIVATE
    src/PtySession_win.cpp
    src/FileJobs_win.cpp
    src/Win.cpp
    src/Win.h
    resources/syn-shell.rc)
  set_target_properties(syn-shell PROPERTIES WIN32_EXECUTABLE ON)
  target_link_libraries(syn-shell PRIVATE shell32 shlwapi ole32 uuid advapi32 dwmapi)

  # Windows Terminal runs console programs and the window isn't one, so
  # the shell goes there (Ctrl+Shift+D) through this: the --attach half
  # of the Linux binary, on its own.
  add_executable(syn-shell-attach
    src/AttachMain_win.cpp
    src/PtySession_win.cpp
    src/PtySession.h
    resources/syn-shell-attach.rc)
  target_link_libraries(syn-shell-attach PRIVATE Qt6::Core Qt6::Network advapi32)
else()
  target_sources(syn-shell PRIVATE
    src/PtySession.cpp
    ${BARCORE_SRC}/syn_bar_notify.c)
endif()

target_include_directories(syn-shell PRIVATE ${BARCORE_SRC})
target_link_libraries(syn-shell PRIVATE Qt6::Widgets Qt6::Network PkgConfig::LIBARCHIVE PkgConfig::VTERM)

if (KF6SyntaxHighlighting_FOUND)
  target_link_libraries(syn-shell PRIVATE KF6::SyntaxHighlighting)
  target_compile_definitions(syn-shell PRIVATE SYN_HAVE_KSYNTAXHIGHLIGHTING)
endif()

if (WIN32)
  # The libraries go beside them: build-windows.ps1 gathers them.
  install(TARGETS syn-shell syn-shell-attach RUNTIME DESTINATION .)
else()
  install(TARGETS syn-shell RUNTIME DESTINATION bin)
  install(FILES resources/syn-shell.desktop
    DESTINATION share/applications)
  install(FILES resources/syn-shell.svg
    DESTINATION share/icons/hicolor/scalable/apps)
  # Its old name, so menus, keybinds and scripts written before the rename
  # (every existing ~/.config copy of the dotfiles) still open it.
  install(CODE "file(CREATE_LINK syn-shell \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/syn-filemanager\" SYMBOLIC)")
endif()
