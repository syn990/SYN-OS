# Building the Windows program on Linux:
#   cmake -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake \
#         -DSDL2_DIR=.../SDL2-2.x/x86_64-w64-mingw32 \
#         -DSDL2_TTF_DIR=.../SDL2_ttf-2.x/x86_64-w64-mingw32
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
