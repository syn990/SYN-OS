/* ------------------------------------------------------------------------
 *                         S Y N - D A S H C A M
 *
 *   Flight recorder for slowdowns: samples /proc/pressure, CPU, memory
 *   and the busiest processes four times a second into a 60-second ring
 *   held in memory, and writes nothing until mark, which keeps that
 *   minute as a snapshot. The slowdown never has to be reproduced; it
 *   was already recorded when you noticed it.
 *
 *   Not built yet: every command below says so and exits 1. The design,
 *   the files to come and the order to build them in are in README.md.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DASHCAM (Performance)
 *   Author     : William Hayward-Holla (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s <command> [args]\n"
		"\n"
		"  start              start recording (daemonizes itself)\n"
		"  stop               stop recording\n"
		"  status [--json]    running or not; --json for waybar\n"
		"  mark               keep the last 60 seconds as a snapshot\n"
		"  list               kept snapshots, newest first\n"
		"  show SNAPSHOT      what was competing for the machine, worst first\n",
		argv0);
}

static int not_built(const char *command) {
	fprintf(stderr, "syn-dashcam: '%s' isn't built yet, see SYN-SOFTWARE/syn-dashcam-wip/README.md\n", command);
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

	/* ---- the recorder ---- */
	if (!strcmp(command, "start")) return not_built(command);
	if (!strcmp(command, "stop")) return not_built(command);
	if (!strcmp(command, "status")) return not_built(command);

	/* ---- snapshots ---- */
	if (!strcmp(command, "mark")) return not_built(command);
	if (!strcmp(command, "list")) return not_built(command);
	if (!strcmp(command, "show")) return not_built(command);

	fprintf(stderr, "syn-dashcam: unknown command '%s'\n", command);
	print_usage(argv[0]);
	return 1;
}
