# Dislocker -- enables to read/write on BitLocker encrypted partitions under
# Linux
# Copyright (C) 2012-2013  Romain Coltel, Hervé Schauer Consultants
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
# USA.

cmake_minimum_required (VERSION 3.10)
project (dislocker C)

# SYN-OS: bindir/libdir/mandir in src/CMakeLists.txt are absolute paths
# fixed at configure time, and the SYN-SOFTWARE build loop only passes
# --prefix /usr at install time — default the prefix to /usr here so
# nothing lands in /usr/local. libdir is pinned to lib: upstream picks
# lib64 whenever it exists, and on Arch /usr/lib64 is a symlink — staged
# into airootfs it would become a real directory shadowing that symlink.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set (CMAKE_INSTALL_PREFIX /usr CACHE PATH "" FORCE)
endif()
if(NOT DEFINED libdir)
	set (libdir "${CMAKE_INSTALL_PREFIX}/lib")
endif()

set (AUTHOR "Romain Coltel")

# SYN-OS: upstream's git branch:commit stamp dropped — vendored here, it
# would stamp SYN-OS's own commit as dislocker's version.


set (VERSION_MAJOR 0)
set (VERSION_MINOR 7)
set (VERSION_RELEASE 2)
set (VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_RELEASE}")

add_subdirectory (${PROJECT_SOURCE_DIR}/src)

configure_file(
	"${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
	IMMEDIATE @ONLY
)
add_custom_target(uninstall
	COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
)
