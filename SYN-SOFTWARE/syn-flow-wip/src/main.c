/* ------------------------------------------------------------------------
 *                            S Y N - F L O W
 *
 *   Draws how things work rather than where they are: services and what
 *   they start, keybinds and what they run, where traffic goes, what a
 *   traced command really did, or a diagram from a sentence. Every one
 *   ends as a themed .dot next to its rendered .svg/.png, so a diagram
 *   can be edited by hand and rendered again.
 *
 *   Not built yet: every command below says so and exits 1. The design,
 *   the files to come and the order to build them in are in README.md.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-FLOW (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s <command> [args] [-o FILE] [--png]\n"
		"\n"
		"From the machine's real state:\n"
		"  boot                    services, their start order, and what each runs\n"
		"  keys                    labwc keybinds -> scripts -> what they call\n"
		"  net                     interfaces, routes, iwd, VPN, SYN-RELAY peers\n"
		"  install                 the installer's stages, read from its scripts\n"
		"\n"
		"From a command:\n"
		"  trace -- CMD [ARGS]     run CMD; every process it started, every file it touched\n"
		"\n"
		"From a sentence:\n"
		"  ask \"SENTENCE\"          a diagram of something that isn't on this machine\n"
		"\n"
		"  -o FILE                 write here instead of ~/Pictures/SYN-FLOW/\n"
		"  --png                   PNG instead of SVG\n",
		argv0);
}

static int not_built(const char *command) {
	fprintf(stderr, "syn-flow: '%s' isn't built yet, see SYN-SOFTWARE/syn-flow-wip/README.md\n", command);
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

	/* ---- categories: read the machine ---- */
	if (!strcmp(command, "boot")) return not_built(command);
	if (!strcmp(command, "keys")) return not_built(command);
	if (!strcmp(command, "net")) return not_built(command);
	if (!strcmp(command, "install")) return not_built(command);

	/* ---- trace: ptrace a command ---- */
	if (!strcmp(command, "trace")) return not_built(command);

	/* ---- ask: a sentence through a model ---- */
	if (!strcmp(command, "ask")) return not_built(command);

	fprintf(stderr, "syn-flow: unknown command '%s'\n", command);
	print_usage(argv[0]);
	return 1;
}
