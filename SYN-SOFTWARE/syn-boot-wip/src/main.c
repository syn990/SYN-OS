/* ------------------------------------------------------------------------
 *                            S Y N - B O O T
 *
 *   Boot timeline for SYN-OS on runit, which has no systemd-analyze:
 *   kernel to init from btime and the kernel log, stage 1 from
 *   timestamps runit/1 writes, every service from its process's start
 *   time in /proc/PID/stat (10 ms, where sv status only gives seconds),
 *   ending when labwc is up.
 *
 *   Not built yet: running it says so and exits 1. The design, the files
 *   to come and the order to build them in are in README.md.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BOOT (Performance)
 *   Author     : William Hayward-Holla (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s [--svg [FILE]]\n"
		"\n"
		"  (no arguments)     every step of the last boot, in order, with start and duration\n"
		"  --svg [FILE]       the same as a themed timeline image\n",
		argv0);
}

static int not_built(const char *what) {
	fprintf(stderr, "syn-boot: %s isn't built yet, see SYN-SOFTWARE/syn-boot-wip/README.md\n", what);
	return 1;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		return not_built("the boot table");
	}

	const char *flag = argv[1];

	if (!strcmp(flag, "--help") || !strcmp(flag, "-h")) {
		print_usage(argv[0]);
		return 0;
	}
	if (!strcmp(flag, "--svg")) return not_built("--svg");

	fprintf(stderr, "syn-boot: unknown argument '%s'\n", flag);
	print_usage(argv[0]);
	return 1;
}
