/* ------------------------------------------------------------------------
 *                             S Y N - L A G
 *
 *   Keypress-to-frame latency per app: types into the app through a
 *   Wayland virtual keyboard, captures its window with
 *   ext-image-copy-capture-v1 (the protocol syn-relay's screen host
 *   already uses), and times the first frame that shows the key by its
 *   presentation_time. Median and worst over N presses.
 *
 *   Not built yet: every mode below says so and exits 1. The design, the
 *   files to come and the order to build them in are in README.md.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-LAG (Performance)
 *   Author     : William Hayward-Holla (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s [-n N] [--app CMD | --suite]\n"
		"\n"
		"  (no arguments)     the focused window: 50 presses, median and worst\n"
		"  -n N               N presses instead of 50\n"
		"  --app CMD          start CMD, wait for its window, measure it, close it\n"
		"  --suite            foot, syn-shell, surf and Falkon in turn, one table\n",
		argv0);
}

static int not_built(const char *what) {
	fprintf(stderr, "syn-lag: %s isn't built yet, see SYN-SOFTWARE/syn-lag-wip/README.md\n", what);
	return 1;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		return not_built("measuring the focused window");
	}

	const char *flag = argv[1];

	if (!strcmp(flag, "--help") || !strcmp(flag, "-h")) {
		print_usage(argv[0]);
		return 0;
	}
	if (!strcmp(flag, "-n")) return not_built("-n");
	if (!strcmp(flag, "--app")) return not_built("--app");
	if (!strcmp(flag, "--suite")) return not_built("--suite");

	fprintf(stderr, "syn-lag: unknown argument '%s'\n", flag);
	print_usage(argv[0]);
	return 1;
}
