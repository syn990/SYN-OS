/* ------------------------------------------------------------------------
 *                       S Y N - I S O - B U I L D E R
 *
 *   Native SYN-OS ISO builder — fully self-contained, no BUILD-ARCHISO.zsh
 *   dependency. Owns its own path/state model (.syn-build/, see
 *   syn_build_paths.h), git commit browsing (syn_git_history.h/
 *   syn_git_extract.h), and the actual build orchestration
 *   (syn_build_runner.h: SYN-SOFTWARE compile loop, mount-safety
 *   cleanup, mkarchiso invocation, ISO archival) — mkarchiso/pacman/
 *   git/cmake stay real external tools, shelled out to directly, same
 *   as this repo's own established convention elsewhere.
 *
 *   Everything past startup lives in one persistent home screen
 *   (syn_tui_dashboard_run(), in syn_tui_dashboard.c) — Target/Repo
 *   checkout/Scratch/Output/Launch/Quit all visible and actionable at
 *   once, for the whole session. Picking a different Target (current
 *   tree / browsed commit / named build / local profile dir / other
 *   repo URL) opens a focused sub-picker that always resolves back to
 *   this same screen, never a separate wizard-then-confirm chain — this
 *   file used to own that flow-then-dashboard split directly, but it
 *   read as "nested menus" rather than one integrated tool, so the
 *   whole session loop moved into syn_tui_dashboard.c, which is now the
 *   thing driving it.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_tui_basic.h"
#include "syn_tui_dashboard.h"
#include "syn_build_paths.h"
#include "syn_repo_locate.h"
#include "syn_build_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* This whole tool runs as root, same as BUILD-ARCHISO.zsh always has —
 * an earlier version tried splitting privilege (unprivileged TUI/build
 * loop, doas only for the final mkarchiso call) to shrink the
 * privileged surface, but that meant every step touching scratch/
 * (wipe, SYN-SOFTWARE build loop) had to guess whether a prior run's
 * root-owned leftovers made it privileged or not, which was a real,
 * recurring source of bugs (stale-ownership wipe failures, log
 * corruption from a build step racing a permission failure) that a
 * single root process structurally can't have. Re-exec under doas here
 * rather than just erroring, so launching this directly (double-click,
 * right-click menu, plain terminal) always works without the caller
 * needing to remember to prefix doas themselves.
 *
 * Goes through syn-uplink-dialpad's --exec mode rather than a bare
 * `execvp("doas", ...)` so the password is collected via the themed
 * graphical prompt (matching syn_tui_modal.h's original, never-wired-up
 * intent for this exact popup) instead of doas's raw /dev/tty text
 * prompt. syn-uplink-dialpad still needs a real controlling terminal to
 * relay this program's own ncurses session through once authenticated
 * — this call site has one (we're still pre-syn_tui_init(), running in
 * whatever terminal launched us, e.g. menu.xml's foot -e wrapper), so
 * the handoff works exactly as if doas's own prompt had been answered
 * directly in that same terminal. */
static void reexec_as_root_if_needed(int argc, char **argv) {
	if (geteuid() == 0) {
		return;
	}

	/* {"/usr/lib/syn-os/syn-uplink-dialpad", "--exec", "--", "doas",
	 *  argv[0], argv[1..], NULL} */
	char **new_argv = malloc(sizeof(char *) * (size_t)(argc + 5));
	if (!new_argv) {
		fprintf(stderr, "syn-iso-builder: out of memory re-execing under doas.\n");
		exit(1);
	}
	new_argv[0] = "/usr/lib/syn-os/syn-uplink-dialpad";
	new_argv[1] = "--exec";
	new_argv[2] = "--";
	new_argv[3] = "doas";
	new_argv[4] = argv[0];
	for (int i = 1; i < argc; i++) {
		new_argv[i + 4] = argv[i];
	}
	new_argv[argc + 4] = NULL;

	execvp(new_argv[0], new_argv);
	/* execvp only returns on failure. */
	fprintf(stderr, "syn-iso-builder: could not re-exec under doas — run this with doas yourself.\n");
	exit(1);
}

/* Plain-stdout counterparts to the dashboard's ncurses log callbacks
 * (append_log_line/redraw_log_on_idle/update_provisional_line in
 * syn_tui_dashboard.c) — same syn_build_run() callback shape, no
 * screen to redraw, so on_idle is a no-op and provisional (\r-redrawn
 * progress) lines just print with a trailing \r like any normal
 * scripted build output would. */
static void headless_on_line(const char *line, void *userdata) {
	(void)userdata;
	printf("%s\n", line);
	fflush(stdout);
}

static void headless_on_provisional(const char *line, void *userdata) {
	(void)userdata;
	printf("\r%s", line);
	fflush(stdout);
}

/* --build-current: skips the interactive dashboard entirely for the one
 * target scriptable callers actually need (this working tree, as-is,
 * no browsing/picking) — for automated/CI-style invocations that can't
 * drive an ncurses session. Everything else (browsing commits, local
 * profile dirs, custom output paths) stays dashboard-only; this isn't
 * meant to become a full CLI, just an escape hatch for the one target
 * kind that never needs a human choice. Deliberately avoids
 * syn_tui_init()/syn_repo_locate_resolve()'s interactive prompt path —
 * autodetection (cwd already looks like a checkout) is guaranteed to
 * succeed for this use case, so no ncurses is ever needed. */
static int run_headless_current_tree_build(void) {
	char repo_root[1024];
	syn_repo_source repo_source;
	if (!syn_repo_locate_resolve(repo_root, sizeof(repo_root), &repo_source)) {
		fprintf(stderr, "syn-iso-builder: --build-current requires running from inside a SYN-OS checkout.\n");
		return 1;
	}

	syn_build_paths paths;
	syn_build_paths_init(&paths);
	if (!syn_build_paths_lock(&paths)) {
		fprintf(stderr, "syn-iso-builder: another instance is already running "
			"(holds the lock at %s/.lock) — only one build at a time.\n", paths.root);
		return 1;
	}

	char today[16];
	time_t now = time(NULL);
	struct tm tm_buf;
	localtime_r(&now, &tm_buf);
	strftime(today, sizeof(today), "%Y-%m-%d", &tm_buf);

	syn_build_target target = {0};
	snprintf(target.profile_dir, sizeof(target.profile_dir), "%s/SYN-ISO-PROFILE", repo_root);
	snprintf(target.build_id, sizeof(target.build_id), "local-%s", today);
	snprintf(target.target_label, sizeof(target.target_label),
		"Current working tree (%s), uncommitted changes included", repo_root);
	target.kind = SYN_TARGET_CURRENT_TREE;
	snprintf(target.detail.current_tree.repo_root, sizeof(target.detail.current_tree.repo_root), "%s", repo_root);

	char out_dir[1024];
	snprintf(out_dir, sizeof(out_dir), "%s", paths.isos);
	char out_name[160];
	snprintf(out_name, sizeof(out_name), "%s", target.build_id);

	printf("syn-iso-builder: building %s\n", target.target_label);

	char final_iso_path[1200] = {0};
	bool ok = syn_build_run(&target, &paths, repo_root, out_dir, out_name,
		headless_on_line, NULL, headless_on_provisional, NULL, final_iso_path);

	if (ok) {
		printf("\nsyn-iso-builder: build finished successfully -> %s\n", final_iso_path);
	} else {
		fprintf(stderr, "\nsyn-iso-builder: build failed — see log above.\n");
	}
	return ok ? 0 : 1;
}

int main(int argc, char **argv) {
	reexec_as_root_if_needed(argc, argv);

	if (argc > 1 && strcmp(argv[1], "--build-current") == 0) {
		return run_headless_current_tree_build();
	}

	syn_tui_init();

	char repo_root[1024];
	syn_repo_source repo_source;
	if (!syn_repo_locate_resolve(repo_root, sizeof(repo_root), &repo_source)) {
		syn_tui_end();
		fprintf(stderr, "syn-iso-builder: could not resolve a SYN-OS checkout — cancelled.\n");
		return 1;
	}

	syn_build_paths paths;
	syn_build_paths_init(&paths);

	/* Two instances against the same scratch path raced each other
	 * mid-build in practice — one process's cmake build directories
	 * disappearing/changing under the other's, producing "missing
	 * CMakeCache.txt" failures that looked like a build-loop bug but
	 * were really just two builds fighting over one scratch tree.
	 * Refuse cleanly rather than let that happen silently again. */
	if (!syn_build_paths_lock(&paths)) {
		syn_tui_end();
		fprintf(stderr, "syn-iso-builder: another instance is already running "
			"(holds the lock at %s/.lock) — only one build at a time.\n", paths.root);
		return 1;
	}

	syn_tui_dashboard_run(&paths, repo_root, repo_source);

	syn_tui_end();
	return 0;
}
