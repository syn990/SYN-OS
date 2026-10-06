/* ------------------------------------------------------------------------
 *                         S Y N - A U T O P S Y
 *
 *   Read-only forensic collector: gathers a machine's state (autostart
 *   points, processes, sockets, logs, devices) into one folder of plain
 *   files, hashes every file in it, and merges every timestamped event
 *   into one timeline. Runs live, or from the ISO against a mounted
 *   disk without ever running a binary from it. diff compares the
 *   machine against the baseline its build (SYN-LFS) or pacman recorded.
 *
 *   Not built yet: every command below says so and exits 1. The design,
 *   the files to come and the order to build them in are in README.md.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-AUTOPSY (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s <command> [args]\n"
		"\n"
		"  collect [-o DIR]               this machine, live\n"
		"  collect --root MNT [-o DIR]    a disk mounted (read-only) at MNT\n"
		"  diff BUNDLE                    files missing, changed, or in no baseline\n"
		"  timeline BUNDLE                the merged timeline, oldest first\n"
		"  verify BUNDLE                  re-hash a bundle against its manifest\n",
		argv0);
}

static int not_built(const char *command) {
	fprintf(stderr, "syn-autopsy: '%s' isn't built yet, see SYN-SOFTWARE/syn-autopsy-wip/README.md\n", command);
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

	if (!strcmp(command, "collect")) return not_built(command);
	if (!strcmp(command, "diff")) return not_built(command);
	if (!strcmp(command, "timeline")) return not_built(command);
	if (!strcmp(command, "verify")) return not_built(command);

	fprintf(stderr, "syn-autopsy: unknown command '%s'\n", command);
	print_usage(argv[0]);
	return 1;
}
