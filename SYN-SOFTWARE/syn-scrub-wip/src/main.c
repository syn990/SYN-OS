/* ------------------------------------------------------------------------
 *                           S Y N - S C R U B
 *
 *   The reverse of syn-autopsy: removes what a machine knows about you,
 *   at a chosen level (user traces, system traces, or the whole disk by
 *   crypto-erase), always from a plan file you've read. run takes the
 *   plan, never a level, so what it does is exactly what you were shown
 *   (same plan-then-yes shape as WH-UNHACK).
 *
 *   Not built yet: every command below says so and exits 1. The design,
 *   the files to come and the order to build them in are in README.md.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SCRUB (Security)
 *   Author     : William Hayward-Holla (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s <command> [args]\n"
		"\n"
		"  plan user [-o PLANFILE]      shell history, browser data, recent files, caches\n"
		"  plan system [-o PLANFILE]    user for every user, plus logs, known_hosts,\n"
		"                               saved Wi-Fi keys, swap, temp files, package caches\n"
		"  plan disk DEVICE [-o PLANFILE]\n"
		"                               the whole disk unreadable (LUKS erase, NVMe/ATA\n"
		"                               secure erase, or zeros), from the live ISO only\n"
		"  run PLANFILE                 carry out exactly that plan\n",
		argv0);
}

static int not_built(const char *command) {
	fprintf(stderr, "syn-scrub: '%s' isn't built yet, see SYN-SOFTWARE/syn-scrub-wip/README.md\n", command);
	return 1;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	const char *command = argv[1];

	if (!strcmp(command, "--help") || !strcmp(command, "-h")) {
		print_usage(argv[0]);
		return 0;
	}

	if (!strcmp(command, "plan")) return not_built(command);
	if (!strcmp(command, "run")) return not_built(command);

	fprintf(stderr, "syn-scrub: unknown command '%s'\n", command);
	print_usage(argv[0]);
	return 1;
}
