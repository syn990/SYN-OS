# dislocker (vendored)

Upstream: https://github.com/Aorimn/dislocker — v0.7.3 release tarball
(sha1 1b40d70030cf914e86da8960fac83e9922ede04a), GPL-2.0-or-later.
Vendored because it is AUR-only on Arch; built by the SYN-SOFTWARE loop like
every other `*-src` project here.

Build host needs `mbedtls3` and `fuse2` installed.

Changes from upstream (each marked `SYN-OS:` in the source, except config.c):

- `CMakeLists.txt` — cmake minimum 3.10; default prefix `/usr`, `libdir`
  pinned to `/usr/lib`; git version stamp removed.
- `src/CMakeLists.txt` — source-dir guard anchored on `/`; `ssl_bindings.h`
  generated into the build dir; Ruby bindings (`dislocker-find`) not built.
- `cmake/FindPolarSSL.cmake` — prefers `/usr/include/mbedtls3` and
  `/usr/lib/mbedtls3` over Arch's mbedtls 4.
- `include/dislocker/ssl_bindings.h.in` — mbedtls 3 names
  (`mbedtls_config.h`, `mbedtls_sha256`).
- `src/config.c` — local variable `true` renamed `trueval` (C23 keyword).
- Removed `.travis.yml` and two stray `src/accesses/*/Makefile`s.
