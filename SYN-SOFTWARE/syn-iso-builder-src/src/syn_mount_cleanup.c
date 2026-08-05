/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "syn_mount_cleanup.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_MOUNTS 256

/* /proc/self/mounts fields are whitespace-separated with octal escapes
 * (\040 for space, etc.) in the path — mount points under a build
 * scratch dir never legitimately contain such characters, so a plain
 * whitespace-delimited scan (no escape decoding) is enough here; this
 * only needs to match paths this tool itself created. */
static int read_mount_points(const char *prefix, char points[][1024]) {
	FILE *f = fopen("/proc/self/mounts", "r");
	if (!f) {
		return 0;
	}

	size_t prefix_len = strlen(prefix);
	int count = 0;
	char line[2048];
	while (count < MAX_MOUNTS && fgets(line, sizeof(line), f)) {
		char device[512], mount_point[1024];
		if (sscanf(line, "%511s %1023s", device, mount_point) != 2) {
			continue;
		}
		(void)device;
		if (strncmp(mount_point, prefix, prefix_len) == 0) {
			snprintf(points[count], 1024, "%s", mount_point);
			count++;
		}
	}
	fclose(f);
	return count;
}

/* Deepest paths first (longest string, since these are all rooted at
 * the same prefix) so a parent directory's unmount never runs while a
 * child mount point underneath it is still mounted. */
static int cmp_deepest_first(const void *a, const void *b) {
	const char *sa = a, *sb = b;
	return (int)strlen(sb) - (int)strlen(sa);
}

/* This whole tool runs as root (see main.c's re-exec), same as the
 * mkarchiso call these mounts come from — plain umount is enough, no
 * doas needed. */
static bool do_umount(bool lazy, const char *path) {
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		if (lazy) {
			execlp("umount", "umount", "-l", path, (char *)NULL);
		} else {
			execlp("umount", "umount", path, (char *)NULL);
		}
		_exit(127);
	}
	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void syn_mount_cleanup(const char *prefix) {
	static char points[MAX_MOUNTS][1024];
	int count = read_mount_points(prefix, points);
	if (count == 0) {
		return;
	}

	qsort(points, (size_t)count, sizeof(points[0]), cmp_deepest_first);

	for (int i = 0; i < count; i++) {
		printf("Unmounting stray mount left by a previous build: %s\n", points[i]);
		fflush(stdout);
		if (do_umount(false, points[i])) {
			continue;
		}
		/* Lazy unmount: detaches immediately, actually frees once no
		 * longer busy — same fallback BUILD-ARCHISO.zsh's own
		 * "umount || umount -l || true" chain used. */
		do_umount(true, points[i]);
	}
}
