/* ------------------------------------------------------------------------
 *                             S Y N - H U M
 *
 *   The machine as sound: samples CPU, memory pressure, network, disk,
 *   new inbound connections and log errors, maps them through
 *   ~/.config/syn-os/hum.conf, and sends syn-bar-core voice levels ten
 *   times a second. syn-bar-core stays the only thing in SYN-OS that
 *   plays sound; this only decides what it should sound like.
 *
 *   Not built yet: every command below says so and exits 1. The design,
 *   the files to come and the order to build them in are in README.md.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holla (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s <command>\n"
		"\n"
		"  start      start sampling and sending to syn-bar-core (daemonizes itself)\n"
		"  stop       stop\n"
		"  status     running or not\n"
		"  test       play each voice on its own, so you know what each one means\n",
		argv0);
}

static int not_built(const char *command) {
	fprintf(stderr, "syn-hum: '%s' isn't built yet, see SYN-SOFTWARE/syn-hum-wip/README.md\n", command);
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

	if (!strcmp(command, "start")) return not_built(command);
	if (!strcmp(command, "stop")) return not_built(command);
	if (!strcmp(command, "status")) return not_built(command);
	if (!strcmp(command, "test")) return not_built(command);

	fprintf(stderr, "syn-hum: unknown command '%s'\n", command);
	print_usage(argv[0]);
	return 1;
}
