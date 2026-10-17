/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_edition.h"
#include "syn_build_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

/* Runs argv as the real user (stderr to /dev/null) and collects its
 * stdout into `out`, NUL-terminated, truncated at `out_len`. Returns its
 * exit status, or -1 if it couldn't be run. */
static int capture_as_real_user(char *const argv[], char *out, size_t out_len) {
	int pipefd[2];
	out[0] = '\0';
	if (pipe(pipefd) != 0) {
		return -1;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return -1;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		int devnull = open("/dev/null", O_RDWR);
		if (devnull >= 0) {
			dup2(devnull, STDIN_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		if (!syn_become_real_user()) {
			_exit(126);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	close(pipefd[1]);
	/* Read to the end even once `out` is full, so the child never blocks
	 * on a full pipe */
	size_t used = 0;
	char chunk[1024];
	ssize_t n;
	while ((n = read(pipefd[0], chunk, sizeof(chunk))) > 0) {
		size_t room = out_len - 1 - used;
		size_t take_n = (size_t)n < room ? (size_t)n : room;
		memcpy(out + used, chunk, take_n);
		used += take_n;
	}
	out[used] = '\0';
	close(pipefd[0]);
	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/* KEY=value into `dst` when `line` is that key's line */
static void take(const char *line, const char *key, char *dst, size_t dst_len) {
	size_t k = strlen(key);
	if (strncmp(line, key, k) == 0 && line[k] == '=') {
		snprintf(dst, dst_len, "%s", line + k + 1);
	}
}

static int cmp_names(const void *a, const void *b) {
	return strcmp((const char *)a, (const char *)b);
}

int syn_edition_list(const char *repo_root, syn_edition *out, int max) {
	char dir_path[1200];
	snprintf(dir_path, sizeof(dir_path), "%s/SYN-EDITIONS", repo_root);
	DIR *d = opendir(dir_path);
	if (!d) {
		return 0;
	}
	char names[SYN_EDITION_MAX][64];
	int count = 0;
	struct dirent *ent;
	while ((ent = readdir(d)) && count < SYN_EDITION_MAX) {
		size_t len = strlen(ent->d_name);
		if (len > 5 && len - 5 < sizeof(names[0]) && strcmp(ent->d_name + len - 5, ".conf") == 0) {
			snprintf(names[count], sizeof(names[count]), "%.*s", (int)(len - 5), ent->d_name);
			count++;
		}
	}
	closedir(d);
	qsort(names, (size_t)count, sizeof(names[0]), cmp_names);

	char build_zsh[1200];
	snprintf(build_zsh, sizeof(build_zsh), "%s/BUILD.zsh", repo_root);
	int read_count = 0;
	for (int i = 0; i < count && read_count < max; i++) {
		char *argv[] = {"zsh", build_zsh, "show", names[i], NULL};
		char text[4096];
		if (capture_as_real_user(argv, text, sizeof(text)) != 0) {
			continue;
		}
		syn_edition *e = &out[read_count];
		memset(e, 0, sizeof(*e));
		snprintf(e->name, sizeof(e->name), "%s", names[i]);
		char *saveptr;
		for (char *line = strtok_r(text, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
			take(line, "Edition", e->edition, sizeof(e->edition));
			take(line, "Summary", e->summary, sizeof(e->summary));
			take(line, "BaseStrat", e->base, sizeof(e->base));
			take(line, "PackageStrat", e->packages, sizeof(e->packages));
			take(line, "KernelStrat", e->kernel, sizeof(e->kernel));
			take(line, "LayerStrat", e->layer, sizeof(e->layer));
			take(line, "OutputStrat", e->output, sizeof(e->output));
			take(line, "Profile", e->profile, sizeof(e->profile));
			take(line, "WorkDir", e->work_dir, sizeof(e->work_dir));
			take(line, "LfsProfile", e->lfs_profile, sizeof(e->lfs_profile));
		}
		syn_edition_read_state(repo_root, e);
		read_count++;
	}
	/* SYN-OS's own first (LayerStrat=syn), then the rest, each by name */
	int syn_count = 0;
	for (int i = 0; i < read_count; i++) {
		if (strcmp(out[i].layer, "syn") == 0) {
			syn_edition e = out[i];
			memmove(&out[syn_count + 1], &out[syn_count], sizeof(syn_edition) * (size_t)(i - syn_count));
			out[syn_count++] = e;
		}
	}
	return read_count;
}

/* `BUILD.zsh status` prints, for an lfs edition, one line per step:
 * "  done     NAME", "  next     NAME" or "  to come  NAME" (after a
 * "  (no build.state ..." note when nothing's recorded); then, for
 * either kind, "newest ISO: PATH (DATE TIME)" or "no ISO built yet" */
bool syn_edition_read_state(const char *repo_root, syn_edition *e) {
	syn_edition_state *st = &e->state;
	memset(st, 0, sizeof(*st));
	char build_zsh[1200];
	snprintf(build_zsh, sizeof(build_zsh), "%s/BUILD.zsh", repo_root);
	char *argv[] = {"zsh", build_zsh, "status", e->name, NULL};
	char text[8192];
	if (capture_as_real_user(argv, text, sizeof(text)) != 0) {
		return false;
	}
	char *saveptr;
	for (char *line = strtok_r(text, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
		if (strncmp(line, "  done     ", 11) == 0) {
			st->steps++;
			st->done++;
		} else if (strncmp(line, "  next     ", 11) == 0) {
			st->steps++;
			snprintf(st->next, sizeof(st->next), "%s", line + 11);
		} else if (strncmp(line, "  to come  ", 11) == 0) {
			st->steps++;
		} else if (strncmp(line, "  (no build.state", 17) == 0) {
			st->unrecorded = true;
		} else if (strncmp(line, "newest ISO: ", 12) == 0) {
			const char *path = line + 12;
			const char *when = strrchr(path, '(');
			int path_len = when && when > path ? (int)(when - path - 1) : (int)strlen(path);
			snprintf(st->newest_iso, sizeof(st->newest_iso), "%.*s", path_len, path);
			if (when) {
				snprintf(st->newest_when, sizeof(st->newest_when), "%.*s",
					(int)strcspn(when + 1, ")"), when + 1);
			}
		}
	}
	struct stat sb;
	if (st->newest_iso[0] && stat(st->newest_iso, &sb) == 0) {
		st->newest_size = (long long)sb.st_size;
	}
	return true;
}

bool syn_edition_doas_ready(void) {
	char *argv[] = {"doas", "-n", "true", NULL};
	char ignored[64];
	return capture_as_real_user(argv, ignored, sizeof(ignored)) == 0;
}
