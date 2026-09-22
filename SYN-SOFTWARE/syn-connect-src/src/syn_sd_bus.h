/* sd-bus from whichever provider CMake found: systemd and elogind install
 * <systemd/sd-bus.h>, basu (SYN-LFS, no systemd) installs <basu/sd-bus.h>.
 * The API is the same. */
#ifndef SYN_SD_BUS_H
#define SYN_SD_BUS_H

#if __has_include(<systemd/sd-bus.h>)
#include <systemd/sd-bus.h>
#else
#include <basu/sd-bus.h>
#endif

#endif
