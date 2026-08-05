/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_build_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <pwd.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

/* doas sets $DOAS_USER to the real invoking user (sudo's equivalent is
 * $SUDO_USER); look that up via getpwnam() for their real home
 * directory rather than trusting $HOME, which is root's own home once
 * re-exec'd (see main.c) and may not even be set correctly for the
 * invoking user's account in this process's environment. Falls back to
 * $HOME if neither var is set (e.g. genuinely logged in as root). */
const char *syn_resolve_real_home(void) {
	const char *invoking_user = getenv("DOAS_USER");
	if (!invoking_user || invoking_user[0] == '\0') {
		invoking_user = getenv("SUDO_USER");
	}
	if (invoking_user && invoking_user[0] != '\0') {
		struct passwd *pw = getpwnam(invoking_user);
		if (pw && pw->pw_dir[0] != '\0') {
			return pw->pw_dir;
		}
	}
	const char *home = getenv("HOME");
	return home ? home : "";
}

/* Build state (multi-GB scratch trees, pacstrap output, finished ISOs)
 * anchors at a fixed XDG data location, NOT inside the repo checkout —
 * it used to be <repo_root>/.syn-build/, which meant cloning/moving/
 * deleting the source tree dragged gigabytes of build state along with
 * it, and every tool's own build output (`*-src-build/` dirs) plus this
 * directory all lived inside the same tree git status walks. Matches
 * the existing ~/.local/share/syn-os/SYN-OS convention syn_repo_locate.c
 * already uses for its own default checkout suggestion — same category,
 * not a second invented location. */
void syn_build_paths_init(syn_build_paths *out) {
	const char *home = syn_resolve_real_home();
	snprintf(out->root, sizeof(out->root), "%s/.local/share/syn-os/iso-builder", home);
	snprintf(out->sources, sizeof(out->sources), "%s/sources", out->root);
	snprintf(out->scratch, sizeof(out->scratch), "%s/scratch", out->root);
	snprintf(out->extracted, sizeof(out->extracted), "%s/extracted", out->root);
	snprintf(out->output, sizeof(out->output), "%s/output", out->root);
	snprintf(out->isos, sizeof(out->isos), "%s/isos", out->root);
}

bool syn_build_paths_lock(const syn_build_paths *paths) {
	char dir_argv_path[1024];
	snprintf(dir_argv_path, sizeof(dir_argv_path), "%s", paths->root);
	/* mkdir -p equivalent — paths->root may not exist yet on a first
	 * run, and open() below needs the parent directory to already be
	 * there. */
	for (char *p = dir_argv_path + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(dir_argv_path, 0755);
			*p = '/';
		}
	}
	mkdir(dir_argv_path, 0755);

	char lock_path[1100];
	snprintf(lock_path, sizeof(lock_path), "%s/.lock", paths->root);

	/* Deliberately leaked: this fd must stay open for the life of the
	 * process for the flock() to mean anything (closing it releases
	 * the lock). The kernel reclaims it on exit — including a crash or
	 * kill -9 — so there's no stale-lockfile cleanup problem the way a
	 * plain "write our pid to a file and check if that pid is still
	 * alive" approach would have. */
	int fd = open(lock_path, O_CREAT | O_RDWR, 0644);
	if (fd < 0) {
		return true; /* can't even create the lockfile — don't block a build over it */
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		return false;
	}
	return true;
}
