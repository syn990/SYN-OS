/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_repo_locate.h"
#include "syn_tui_basic.h"
#include "syn_build_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

static bool looks_like_checkout(const char *dir) {
	char probe[1200];
	snprintf(probe, sizeof(probe), "%s/SYN-ISO-PROFILE/profiledef.sh", dir);
	if (access(probe, F_OK) != 0) {
		return false;
	}
	snprintf(probe, sizeof(probe), "%s/SYN-SOFTWARE", dir);
	struct stat st;
	return stat(probe, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool autodetect(char *out, size_t out_len) {
	char cursor[1024];
	if (!getcwd(cursor, sizeof(cursor))) {
		return false;
	}
	for (int depth = 0; depth < 8; depth++) {
		if (looks_like_checkout(cursor)) {
			snprintf(out, out_len, "%s", cursor);
			return true;
		}
		char *slash = strrchr(cursor, '/');
		if (!slash || slash == cursor) {
			break;
		}
		*slash = '\0';
	}
	return false;
}

static void config_path(char *out, size_t out_len) {
	snprintf(out, out_len, "%s/.config/syn-os/iso-builder.conf", syn_resolve_real_home());
}

/* Same manual key=value line scanning syn_theme.c already uses for its
 * own config file — no new parsing approach introduced. */
static bool read_config(char *out, size_t out_len) {
	char path[1024];
	config_path(path, sizeof(path));

	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}

	bool found = false;
	char line[1200];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "repo_path=", 10) != 0) {
			continue;
		}
		const char *v = line + 10;
		size_t len = strlen(v);
		while (len > 0 && (v[len - 1] == '\n' || v[len - 1] == '\r')) {
			len--;
		}
		if (len > 0 && len < out_len) {
			memcpy(out, v, len);
			out[len] = '\0';
			found = looks_like_checkout(out);
		}
		break;
	}
	fclose(f);
	return found;
}

static bool write_config(const char *repo_path) {
	char path[1024];
	config_path(path, sizeof(path));

	char dir[1024];
	snprintf(dir, sizeof(dir), "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash) {
		*slash = '\0';
		char *argv[] = {"mkdir", "-p", dir, NULL};
		pid_t pid = fork();
		if (pid == 0) {
			execvp("mkdir", argv);
			_exit(127);
		} else if (pid > 0) {
			int status;
			waitpid(pid, &status, 0);
		}
	}

	FILE *f = fopen(path, "w");
	if (!f) {
		return false;
	}
	fprintf(f, "repo_path=%s\n", repo_path);
	fclose(f);
	return true;
}

static bool clone_into(const char *dest) {
	char parent[1024];
	snprintf(parent, sizeof(parent), "%s", dest);
	char *slash = strrchr(parent, '/');
	if (slash) {
		*slash = '\0';
		char *mkdir_argv[] = {"mkdir", "-p", parent, NULL};
		pid_t mkdir_pid = fork();
		if (mkdir_pid == 0) {
			execvp("mkdir", mkdir_argv);
			_exit(127);
		} else if (mkdir_pid > 0) {
			int status;
			waitpid(mkdir_pid, &status, 0);
		}
	}

	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		execlp("git", "git", "clone", "https://github.com/syn990/SYN-OS.git", dest, (char *)NULL);
		_exit(127);
	}
	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool syn_repo_locate_resolve(char *out, size_t out_len, syn_repo_source *source_out) {
	if (autodetect(out, out_len)) {
		if (source_out) *source_out = SYN_REPO_SOURCE_AUTODETECTED;
		return true;
	}

	if (read_config(out, out_len)) {
		if (source_out) *source_out = SYN_REPO_SOURCE_CONFIG;
		return true;
	}

	char suggestion[1024];
	snprintf(suggestion, sizeof(suggestion), "%s/.local/share/syn-os/SYN-OS", syn_resolve_real_home());

	char chosen[1024];
	if (syn_tui_text_prompt("SYN-OS checkout location (will clone if empty)", suggestion, chosen, sizeof(chosen)) != 0) {
		return false;
	}
	if (chosen[0] == '\0') {
		snprintf(chosen, sizeof(chosen), "%s", suggestion);
	}

	if (!looks_like_checkout(chosen)) {
		if (!clone_into(chosen) || !looks_like_checkout(chosen)) {
			return false;
		}
	}

	write_config(chosen);
	snprintf(out, out_len, "%s", chosen);
	if (source_out) *source_out = SYN_REPO_SOURCE_PROMPTED;
	return true;
}
