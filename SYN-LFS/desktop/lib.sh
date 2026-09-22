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

# svc NAME: install files/sv/NAME as a runit service and enable it
svc() {
	install -d "/etc/sv/$1" /var/service
	install -m755 "$SYN_FILES/sv/$1/"* "/etc/sv/$1/"
	ln -sfn "/etc/sv/$1" "/var/service/$1"
}

# syn_tool NAME: build one of SYN-OS's own tools from SYN-SOFTWARE/NAME-src
syn_tool() {
	cmake -B syn-build -S "$SYN_REPO/SYN-SOFTWARE/$1-src" -D CMAKE_BUILD_TYPE=Release
	cmake --build syn-build
	cmake --install syn-build --prefix /usr
}
