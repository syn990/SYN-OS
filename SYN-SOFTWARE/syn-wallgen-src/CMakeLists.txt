cmake_minimum_required(VERSION 3.16)
project(syn-wallgen LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPNG REQUIRED libpng)

add_executable(syn-wallgen
  src/main.c
  src/syn_wallgen_theme.c
  src/syn_wallgen_rng.c
  src/syn_wallgen_render.c
  src/syn_wallgen_png.c
)

target_include_directories(syn-wallgen PRIVATE ${LIBPNG_INCLUDE_DIRS})
target_link_libraries(syn-wallgen PRIVATE ${LIBPNG_LIBRARIES} m)
target_compile_options(syn-wallgen PRIVATE ${LIBPNG_CFLAGS_OTHER})

install(TARGETS syn-wallgen RUNTIME DESTINATION lib/syn-os)
