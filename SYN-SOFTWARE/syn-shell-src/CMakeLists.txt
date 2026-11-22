cmake_minimum_required(VERSION 3.16)
project(syn-shell LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network OpenGLWidgets)
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

# syn_bar_notify.c/.h: a copy of syn-bar-core's (SYN-OS,
# SYN-SOFTWARE/syn-bar-core-src) so this repository builds on its own.
# FileOps.cpp asks syn-bar-core, over its socket, to sound a named
# meaning (syn_bar_notify_meaning(), extern "C"); syn-bar-core itself
# makes the sound. Copy it again if syn-bar-core changes the protocol.
set(BARCORE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/notify)

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

  # Disk space and walk mode (src/space/): syn-shell --space.
  # Plain C on SDL2 + OpenGL 3.3, its font and the SYN-OS themes compiled
  # in (cmake/embed.cmake) from assets/themes: a copy of SYN-OS's
  # (SYN-ISO-PROFILE/.../.config/syn-os/themes). Copy them again when
  # SYN-OS gains a theme.
  pkg_search_module(SDL2 REQUIRED sdl2 sdl2-compat) # sdl2-compat: SDL2's API on SDL3
  pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
  pkg_check_modules(GL REQUIRED gl)
  set(SPACE_THEME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets/themes)
  set(SPACE_FONT ${CMAKE_CURRENT_SOURCE_DIR}/assets/SYNTerminal-Regular.ttf)
  file(GLOB SPACE_THEME_FILES ${SPACE_THEME_DIR}/*.theme)
  add_custom_command(
    OUTPUT space_assets.c
    COMMAND ${CMAKE_COMMAND} -DFONT=${SPACE_FONT} -DTHEME_DIR=${SPACE_THEME_DIR}
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/space_assets.c
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${SPACE_FONT} ${SPACE_THEME_FILES} cmake/embed.cmake)
  set(SPACE_SRC
    src/space/main.c src/space/app.c src/space/scan.c src/space/disks.c
    src/space/theme.c src/space/kinds.c src/space/blocks.c src/space/graph.c
    src/space/draw.c src/space/gl.c src/space/fileops.c src/space/icons_data.c
    src/space/walk.c src/space/world.c ${CMAKE_CURRENT_BINARY_DIR}/space_assets.c)
  target_sources(syn-shell PRIVATE ${SPACE_SRC}
    src/SpaceView.cpp src/SpaceView.h src/SpaceTree.cpp src/SpaceTree.h
    src/WalkOverlay.cpp src/WalkOverlay.h)
  target_link_libraries(syn-shell PRIVATE Qt6::OpenGLWidgets)

  # Walking holds the mouse as a game does (src/PointerLock.cpp): on
  # Wayland through the pointer-constraints and relative-pointer
  # protocols, their client code made here by wayland-scanner. Without
  # them it still holds it on X11, and on Wayland the plain mouse turns you.
  target_sources(syn-shell PRIVATE src/PointerLock.cpp src/PointerLock.h)
  pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
  pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
  find_program(WAYLAND_SCANNER wayland-scanner)
  set(QT_NO_PRIVATE_MODULE_WARNING ON)
  find_package(Qt6 QUIET COMPONENTS GuiPrivate)
  if (WAYLAND_CLIENT_FOUND AND WAYLAND_PROTOCOLS_DIR AND WAYLAND_SCANNER AND TARGET Qt6::GuiPrivate)
    foreach(proto pointer-constraints relative-pointer)
      set(xml ${WAYLAND_PROTOCOLS_DIR}/unstable/${proto}/${proto}-unstable-v1.xml)
      set(base ${CMAKE_CURRENT_BINARY_DIR}/${proto}-unstable-v1)
      add_custom_command(OUTPUT ${base}-client-protocol.h ${base}-protocol.c
        COMMAND ${WAYLAND_SCANNER} client-header ${xml} ${base}-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} private-code ${xml} ${base}-protocol.c
        DEPENDS ${xml})
      target_sources(syn-shell PRIVATE ${base}-client-protocol.h ${base}-protocol.c)
    endforeach()
    target_include_directories(syn-shell PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(syn-shell PRIVATE PkgConfig::WAYLAND_CLIENT Qt6::GuiPrivate)
    target_compile_definitions(syn-shell PRIVATE SYN_HAVE_POINTER_LOCK)
  else()
    message(STATUS "syn-shell: no Wayland pointer lock (wayland-client, wayland-protocols, wayland-scanner, Qt6 GuiPrivate)")
  endif()
  set_source_files_properties(${SPACE_SRC} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;${SDL2_CFLAGS_OTHER}"
    COMPILE_DEFINITIONS "_DEFAULT_SOURCE"
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/src/space")
  set_target_properties(syn-shell PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
  target_include_directories(syn-shell PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
  target_link_libraries(syn-shell PRIVATE ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES} ${GL_LIBRARIES} m)

  # Options for driving the window from a script (pictures of it, clicks
  # and keys in the space view): src/TestDrive.cpp. Off in the package.
  option(SYN_SHELL_TEST "build the --shot / --view-* test options" OFF)
  if (SYN_SHELL_TEST)
    target_sources(syn-shell PRIVATE src/TestDrive.cpp src/TestDrive.h)
    target_compile_definitions(syn-shell PRIVATE SYN_SHELL_TEST)
  endif()
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
