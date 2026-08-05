/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_git_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

static bool dir_exists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Small stable hash (FNV-1a) of the URL, used only to give arbitrary
 * custom repo URLs a deterministic, filesystem-safe cache directory
 * name — not a security-sensitive use, collision risk is irrelevant at
 * the scale of "how many custom repo URLs one user tries". */
static unsigned long fnv1a(const char *s) {
	unsigned long h = 2166136261UL;
	for (; *s; s++) {
		h ^= (unsigned char)*s;
		h *= 16777619UL;
	}
	return h;
}

void syn_git_mirror_path(char *out, size_t out_len, const char *sources_dir, const char *remote_url) {
	if (strcmp(remote_url, SYN_GIT_MAIN_URL) == 0) {
		snprintf(out, out_len, "%s/syn-os.git", sources_dir);
	} else if (strcmp(remote_url, SYN_GIT_RTOS_URL) == 0) {
		snprintf(out, out_len, "%s/syn-rtos.git", sources_dir);
	} else {
		snprintf(out, out_len, "%s/custom/%08lx.git",
			sources_dir, fnv1a(remote_url) & 0xffffffffUL);
	}
}

/* fork()+execvp()+waitpid, never system()/popen() for commands with no
 * output we need to read — matches syn-relay-src's documented
 * "avoid a shell for a fixed argv" convention. */
static bool run_git_ex(char *const argv[], bool quiet) {
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		if (quiet) {
			int devnull = open("/dev/null", O_WRONLY);
			if (devnull >= 0) {
				dup2(devnull, STDERR_FILENO);
				close(devnull);
			}
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool run_git(char *const argv[]) {
	return run_git_ex(argv, false);
}

static void ensure_parent_dir(const char *path) {
	char parent[1024];
	snprintf(parent, sizeof(parent), "%s", path);
	char *slash = strrchr(parent, '/');
	if (slash) {
		*slash = '\0';
		char *argv[] = {"mkdir", "-p", parent, NULL};
		pid_t pid = fork();
		if (pid == 0) {
			execvp("mkdir", argv);
			_exit(127);
		} else if (pid > 0) {
			int status;
			waitpid(pid, &status, 0);
		}
	}
}

/* run_git despite the name just forks+execvp's whatever argv[0] is —
 * reused here for "rm" since the shape (fork/exec/waitpid, no shell)
 * is identical. */
static void remove_dir(const char *path) {
	char *argv[] = {"rm", "-rf", (char *)path, NULL};
	run_git(argv);
}

/* `git --git-dir=<path> rev-parse --git-dir` only succeeds against a
 * real, intact git directory — plain dir_exists() isn't enough (a
 * partial/corrupted clone, e.g. interrupted mid-transfer, still leaves
 * a directory behind, which `fetch` against would just silently fail
 * on every subsequent run forever, per the pre-existing bug this
 * function fixes: a stale-but-directory-shaped mirror was previously
 * indistinguishable from a real one). */
static bool mirror_is_valid(const char *mirror) {
	if (!dir_exists(mirror)) {
		return false;
	}
	char git_dir_arg[1064];
	snprintf(git_dir_arg, sizeof(git_dir_arg), "--git-dir=%s", mirror);
	char *argv[] = {"git", git_dir_arg, "rev-parse", "--git-dir", NULL};
	return run_git_ex(argv, true);
}

bool syn_git_ensure_mirror(const char *sources_dir, const char *remote_url) {
	char mirror[1024];
	syn_git_mirror_path(mirror, sizeof(mirror), sources_dir, remote_url);

	if (dir_exists(mirror) && !mirror_is_valid(mirror)) {
		remove_dir(mirror);
	}

	if (!dir_exists(mirror)) {
		ensure_parent_dir(mirror);
		char *argv[] = {"git", "clone", "--mirror", (char *)remote_url, mirror, NULL};
		return run_git(argv);
	}

	char git_dir_arg[1064];
	snprintf(git_dir_arg, sizeof(git_dir_arg), "--git-dir=%s", mirror);
	char *argv[] = {"git", git_dir_arg, "fetch", "--quiet", "origin",
		"+refs/heads/*:refs/heads/*", NULL};
	/* Fetch failure (offline, etc) falls through to using whatever's
	 * already cached, same as BUILD-ARCHISO.zsh's own "|| true" — a
	 * stale-but-present mirror is still browsable. */
	run_git(argv);
	return true;
}

/* Field separator unlikely to appear in a commit subject/author —
 * matches the manual-delimiter-parsing style already used throughout
 * this session (JSON/.desktop parsing) rather than pulling in a real
 * parser for git's own --format output. */
#define FIELD_SEP "\x1f"

int syn_git_list_commits(const char *sources_dir, const char *remote_url,
	syn_git_commit *out, int max) {
	char mirror[1024];
	syn_git_mirror_path(mirror, sizeof(mirror), sources_dir, remote_url);
	if (!dir_exists(mirror)) {
		return -1;
	}

	char git_dir_arg[1064];
	snprintf(git_dir_arg, sizeof(git_dir_arg), "--git-dir=%s", mirror);

	int pipefd[2];
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
		char format_arg[64];
		snprintf(format_arg, sizeof(format_arg), "--format=%%h" FIELD_SEP "%%H" FIELD_SEP "%%as" FIELD_SEP "%%an" FIELD_SEP "%%s");
		char max_count_arg[32];
		snprintf(max_count_arg, sizeof(max_count_arg), "--max-count=%d", max);
		char *argv[] = {"git", git_dir_arg, "log", format_arg, max_count_arg, NULL};
		execvp("git", argv);
		_exit(127);
	}
	close(pipefd[1]);

	FILE *f = fdopen(pipefd[0], "r");
	if (!f) {
		close(pipefd[0]);
		int status;
		waitpid(pid, &status, 0);
		return -1;
	}

	int count = 0;
	char line[1024];
	while (count < max && fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}

		char *fields[5] = {0};
		char *p = line;
		int field_idx = 0;
		fields[field_idx++] = p;
		while (field_idx < 5 && (p = strchr(p, FIELD_SEP[0])) != NULL) {
			*p = '\0';
			p++;
			fields[field_idx++] = p;
		}
		if (field_idx != 5) {
			continue;
		}

		syn_git_commit *c = &out[count];
		snprintf(c->short_sha, sizeof(c->short_sha), "%s", fields[0]);
		snprintf(c->full_sha, sizeof(c->full_sha), "%s", fields[1]);
		snprintf(c->date, sizeof(c->date), "%s", fields[2]);
		snprintf(c->author, sizeof(c->author), "%s", fields[3]);
		snprintf(c->subject, sizeof(c->subject), "%s", fields[4]);
		count++;
	}
	fclose(f);

	int status;
	waitpid(pid, &status, 0);
	return count;
}
