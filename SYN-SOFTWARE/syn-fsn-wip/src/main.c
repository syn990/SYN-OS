/* ------------------------------------------------------------------------
 *                             S Y N - F S N
 *
 *   The file system as a 3D picture, after SGI's fsn: folders as
 *   platforms, files as blocks whose height is their size, colored by
 *   type or age, recent changes glowing. A viewer, not a game: turn it,
 *   zoom it, click a folder to focus it, click a file to see it. The
 *   layout is plain C with no graphics (--dump prints it); the renderer
 *   is its own small SDL2 + OpenGL 3.3 one.
 *
 *   Not built yet: every mode below says so and exits 1. The design, the
 *   files to come and the order to build them in are in README.md.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-FSN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s [--by size|age|type] [--dump] [DIR]\n"
		"\n"
		"  [DIR]                show DIR (your home folder if none)\n"
		"  --by size|age|type   what height and color mean (default: size and type)\n"
		"  --dump [DIR]         print the layout as text, no window\n",
		argv0);
}

static int not_built(const char *what) {
	fprintf(stderr, "syn-fsn: %s isn't built yet, see SYN-SOFTWARE/syn-fsn-wip/README.md\n", what);
	return 1;
}

int main(int argc, char **argv) {
	if (argc >= 2) {
		const char *flag = argv[1];
		if (!strcmp(flag, "--help") || !strcmp(flag, "-h")) {
			print_usage(argv[0]);
			return 0;
		}
		if (!strcmp(flag, "--dump")) return not_built("--dump");
		if (!strcmp(flag, "--by")) return not_built("--by");
	}

	return not_built("the 3D view");
}
