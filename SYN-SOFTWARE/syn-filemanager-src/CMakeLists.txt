cmake_minimum_required(VERSION 3.16)
project(syn-filemanager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(syn-filemanager
  src/main.cpp
  src/MainWindow.cpp
  src/MainWindow.h
  src/FileOps.cpp
  src/FileOps.h
)

target_link_libraries(syn-filemanager PRIVATE Qt6::Widgets)

install(TARGETS syn-filemanager RUNTIME DESTINATION bin)
install(FILES resources/syn-filemanager.desktop
  DESTINATION share/applications)
