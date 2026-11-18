cmake_minimum_required(VERSION 3.16)
project(syn-dirstat LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

# The font and the SYN-OS themes go into the program (cmake/embed.cmake),
# the themes straight from the ISO profile, so the program is one file and
# always has the themes the desktop has.
set(THEME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/DotfileOverlay/etc/skel/.config/syn-os/themes)
set(FONT ${CMAKE_CURRENT_SOURCE_DIR}/assets/SYNTerminal-Regular.ttf)
file(GLOB THEME_FILES ${THEME_DIR}/*.theme)
add_custom_command(
  OUTPUT assets.c
  COMMAND ${CMAKE_COMMAND} -DFONT=${FONT} -DTHEME_DIR=${THEME_DIR}
          -DOUT=${CMAKE_CURRENT_BINARY_DIR}/assets.c
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${FONT} ${THEME_FILES} cmake/embed.cmake
)

add_executable(syn-dirstat
  src/main.c
  src/app.c
  src/scan.c
  src/disks.c
  src/theme.c
  src/kinds.c
  src/blocks.c
  src/graph.c
  src/draw.c
  src/gl.c
  ${CMAKE_CURRENT_BINARY_DIR}/assets.c
)
target_include_directories(syn-dirstat PRIVATE src)
target_compile_options(syn-dirstat PRIVATE -Wall -Wextra)
target_compile_definitions(syn-dirstat PRIVATE _DEFAULT_SOURCE)

if(WIN32)
  # MinGW-w64, on Windows (MSYS2) or across from Linux with
  # cmake/mingw-w64.cmake. SDL2_DIR and SDL2_TTF_DIR are the
  # x86_64-w64-mingw32 folders of SDL's own mingw development packages.
  # Linked statically: the .exe needs no DLL but Windows's own.
  target_sources(syn-dirstat PRIVATE src/win/syn-dirstat.rc)
  target_include_directories(syn-dirstat PRIVATE ${SDL2_DIR}/include ${SDL2_DIR}/include/SDL2 ${SDL2_TTF_DIR}/include)
  target_link_directories(syn-dirstat PRIVATE ${SDL2_DIR}/lib ${SDL2_TTF_DIR}/lib)
  target_link_libraries(syn-dirstat PRIVATE
    -static -mwindows mingw32 SDL2main SDL2_ttf SDL2 opengl32
    usp10 rpcrt4 m dinput8 dxguid dxerr8 user32 gdi32 winmm imm32 ole32 oleaut32
    shell32 setupapi version uuid)
else()
  find_package(PkgConfig REQUIRED)
  # sdl2 (SDL2 proper), or sdl2-compat, SDL2's API on SDL3 (SYN-OS on runit)
  pkg_search_module(SDL2 REQUIRED sdl2 sdl2-compat)
  pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)
  pkg_check_modules(GL REQUIRED gl)
  target_include_directories(syn-dirstat PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
  target_link_libraries(syn-dirstat PRIVATE ${SDL2_LIBRARIES} ${SDL2_TTF_LIBRARIES} ${GL_LIBRARIES} m)
  target_compile_options(syn-dirstat PRIVATE ${SDL2_CFLAGS_OTHER})
endif()

install(TARGETS syn-dirstat RUNTIME DESTINATION lib/syn-os)
