/* ------------------------------------------------------------------------
 *                         S Y N - D I R S T A T
 *
 *   Where the space on a disk has gone, on Linux and Windows. It scans a
 *   disk, a folder or a mounted network share and shows it three ways at
 *   once: a tree list that sorts by any column, a block map where every
 *   file is a rectangle as big as the space it takes, and a node graph of
 *   the biggest folders that you walk into. The map and the graph each
 *   turn between 2D and 3D. It wears the active SYN-OS theme.
 *
 *   SDL2 for the window and input, OpenGL 3.3 for everything drawn.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "app.h"
#include "scan.h"

static void print_usage(const char *argv0) {
	fprintf(stderr,
		"Usage: %s --space [options] [PATH]\n"
		"\n"
		"  PATH              scan this disk, folder or network share at once\n"
		"                    (none: start at the list of disks)\n"
		"  --graph           start in the node graph, not the block map\n"
		"  --3d              start in 3D\n"
		"  --theme NAME      wear this SYN-OS theme, e.g. SYN-OS-HALO-CYAN\n"
		"  --dump PATH       print the tree as text, no window\n"
		"  --depth N         how many levels --dump prints (default 2)\n"
		"  --shot FILE.ppm   save the first picture (of PATH, or of the disks) and leave\n"
		"  --shot-at MS      with --shot: the picture MS after the start, mid-scan or not\n"
		"  --levels N        start the DEPTH slider at N levels, 1 .. 11 (default all)\n"
		"  --height X        start the HEIGHT slider at X, 0 (flat) .. 3 (default 1)\n"
		"  --spin X          start the SPIN slider at X times SYN-TREK's speed, 0 .. 3 (default 1)\n"
		"  --walk            start in walk mode, in the folder scanned\n",
		argv0);
}

static void dump(const Node *n, int depth, int levels) {
	char size[32];
	format_size(n->size, size, sizeof size);
	printf("%12s %9llu  %*s%s%s%s\n", size, (unsigned long long)n->files, depth * 2, "", n->name,
	       n->is_dir && n->parent ? "/" : "", n->unread ? "  (could not be read)" : "");
	for (int i = 0; depth < levels && i < n->nkids; i++) {
		dump(n->kids[i], depth + 1, levels);
	}
}

static int run_dump(const char *path, int levels) {
#ifdef _WIN32
	/* a window program has no terminal of its own: write to the one it was started from */
	if (AttachConsole(ATTACH_PARENT_PROCESS)) {
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
	}
#endif
	ScanProgress progress = {0};
	char err[1024];
	Node *root = scan_tree(path, &progress, err, sizeof err);
	if (!root) {
		fprintf(stderr, "syn-shell: %s\n", err);
		return 1;
	}
	dump(root, 0, levels);
	scan_free(root);
	return 0;
}

/* syn-shell --space ARGS (main.cpp) */
int space_main(int argc, char **argv) {
	AppOptions opt = {0};
	opt.lift = -1;
	opt.spin = -1;
	const char *dump_path = NULL;
	int levels = 2;
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i], *value = i + 1 < argc ? argv[i + 1] : NULL;
		if (!strcmp(arg, "--graph")) {
			opt.graph = true;
		} else if (!strcmp(arg, "--3d")) {
			opt.solid = true;
		} else if (!strcmp(arg, "--theme") && value) {
			opt.theme = argv[++i];
		} else if (!strcmp(arg, "--dump") && value) {
			dump_path = argv[++i];
		} else if (!strcmp(arg, "--depth") && value) {
			levels = atoi(argv[++i]);
		} else if (!strcmp(arg, "--shot") && value) {
			opt.shot = argv[++i];
		} else if (!strcmp(arg, "--shot-at") && value) {
			opt.shot_at = atoi(argv[++i]);
		} else if (!strcmp(arg, "--levels") && value) {
			opt.levels = atoi(argv[++i]);
		} else if (!strcmp(arg, "--height") && value) {
			opt.lift = (float)atof(argv[++i]);
		} else if (!strcmp(arg, "--walk")) {
			opt.walk = true;
		} else if (!strcmp(arg, "--spin") && value) {
			opt.spin = (float)atof(argv[++i]);
		} else if (arg[0] == '-' || opt.path) {
			print_usage(argv[0]);
			return 2;
		} else {
			opt.path = arg;
		}
	}
	return dump_path ? run_dump(dump_path, levels) : app_run(&opt);
}
