# Helpers loaded into every recipe's build() shell (see syn-pkg.sh).
# The build dir is syn-build rather than build: plenty of tarballs (the
# *mm C++ bindings, p11-kit) already ship a build/ directory of their own.

# meson_build [options...]: configure, build and install with meson
meson_build() {
	meson setup syn-build --prefix=/usr --buildtype=release --wrap-mode=nofallback "$@"
	ninja -C syn-build
	ninja -C syn-build install
}

# cmake_build [options...]: the same with CMake and Ninja
cmake_build() {
	cmake -B syn-build -G Ninja \
		-D CMAKE_INSTALL_PREFIX=/usr \
		-D CMAKE_INSTALL_LIBDIR=lib \
		-D CMAKE_BUILD_TYPE=Release \
		-D CMAKE_SKIP_INSTALL_RPATH=ON \
		"$@"
	ninja -C syn-build
	ninja -C syn-build install
}

# 32-bit builds (for Steam) on the multilib base: the lib32 cross file and
# the i686 pkg-config the multilib book sets up, installed into a staging
# dir and only its /usr/lib32 copied over, since the 64-bit build of the
# same package already put headers, tools and data in place.
install_lib32() {
	cp -Rv "$1/usr/lib32/." /usr/lib32/
}

# meson_build32 [options...]
meson_build32() {
	meson setup syn-build32 --prefix=/usr --buildtype=release --wrap-mode=nofallback \
		--cross-file=lib32 "$@"
	ninja -C syn-build32
	DESTDIR=$PWD/syn-dest32 ninja -C syn-build32 install
	install_lib32 syn-dest32
}

# cmake_build32 [options...]
cmake_build32() {
	CFLAGS=-m32 CXXFLAGS=-m32 PKG_CONFIG=i686-pc-linux-gnu-pkg-config \
	cmake -B syn-build32 -G Ninja \
		-D CMAKE_INSTALL_PREFIX=/usr \
		-D CMAKE_INSTALL_LIBDIR=lib32 \
		-D CMAKE_BUILD_TYPE=Release \
		-D CMAKE_SKIP_INSTALL_RPATH=ON \
		"$@"
	ninja -C syn-build32
	DESTDIR=$PWD/syn-dest32 ninja -C syn-build32 install
	install_lib32 syn-dest32
}

# configure32 [options...]: autotools; CC32 overrides the compiler
configure32() {
	CC=${CC32:-gcc -m32} CXX=${CXX32:-g++ -m32} PKG_CONFIG=i686-pc-linux-gnu-pkg-config \
	./configure --prefix=/usr --libdir=/usr/lib32 --sysconfdir=/etc --localstatedir=/var \
		--host=i686-pc-linux-gnu --disable-static "$@"
	make
	make DESTDIR=$PWD/syn-dest32 install
	install_lib32 syn-dest32
}

# svc NAME [off]: install files/sv/NAME as a runit service with an svlogd
# logger writing /var/log/sv/NAME (syn-sysmon's Logs view reads these),
# and enable it, unless "off" is given: then it's there to switch on later
svc() {
	install -d "/etc/sv/$1/log" /var/service
	install -m755 "$SYN_FILES/sv/$1/run" "/etc/sv/$1/"
	printf '#!/bin/sh\ninstall -d -m755 /var/log/sv/%s\nexec svlogd -tt /var/log/sv/%s\n' \
		"$1" "$1" > "/etc/sv/$1/log/run"
	chmod 755 "/etc/sv/$1/log/run"
	[ "$2" = off ] || ln -sfn "/etc/sv/$1" "/var/service/$1"
}

# syn_tool NAME: build one of SYN-OS's own tools from SYN-SOFTWARE/NAME-src
syn_tool() {
	cmake -B syn-build -S "$SYN_REPO/SYN-SOFTWARE/$1-src" -D CMAKE_BUILD_TYPE=Release
	cmake --build syn-build
	cmake --install syn-build --prefix /usr
}
