/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_git_extract.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/stat.h>

/* Captures stderr (not stdout — these are mkdir/rm, no useful stdout)
 * into `err_out` when the command fails, instead of letting it print
 * straight to the terminal underneath the ncurses screen where it was
 * invisible (this is exactly how a real "permission denied" from mkdir
 * went unseen before, surfacing only as an opaque "extraction failed"). */
static bool run_cmd_capture(char *const argv[], char *err_out, size_t err_out_len) {
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		return false;
	}
	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDERR_FILENO);
		close(pipefd[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(pipefd[1]);
	if (err_out && err_out_len > 0) {
		ssize_t n = read(pipefd[0], err_out, err_out_len - 1);
		err_out[n > 0 ? n : 0] = '\0';
		size_t len = strlen(err_out);
		while (len > 0 && (err_out[len - 1] == '\n' || err_out[len - 1] == '\r')) {
			err_out[--len] = '\0';
		}
	}
	close(pipefd[0]);
	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool run_cmd(char *const argv[]) {
	return run_cmd_capture(argv, NULL, 0);
}

/* git archive <sha> | tar -x -C <dest>, piped directly between two
 * children (no shell, no intermediate tarball file) — mirrors
 * BUILD-ARCHISO.zsh's own `git archive ... | tar -x ...` for the
 * --build= path, just without a profile_path restriction (the whole
 * tree, since we don't know the profile path yet for an arbitrary
 * commit). */
static bool archive_extract(const char *mirror_path, const char *commit_sha, const char *dest) {
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		return false;
	}

	pid_t git_pid = fork();
	if (git_pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}
	if (git_pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		char git_dir_arg[1064];
		snprintf(git_dir_arg, sizeof(git_dir_arg), "--git-dir=%s", mirror_path);
		char *argv[] = {"git", git_dir_arg, "archive", (char *)commit_sha, NULL};
		execvp("git", argv);
		_exit(127);
	}

	pid_t tar_pid = fork();
	if (tar_pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		int status;
		waitpid(git_pid, &status, 0);
		return false;
	}
	if (tar_pid == 0) {
		close(pipefd[1]);
		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[0]);
		char *argv[] = {"tar", "-x", "-C", (char *)dest, NULL};
		execvp("tar", argv);
		_exit(127);
	}

	close(pipefd[0]);
	close(pipefd[1]);

	int git_status, tar_status;
	waitpid(git_pid, &git_status, 0);
	waitpid(tar_pid, &tar_status, 0);
	return WIFEXITED(git_status) && WEXITSTATUS(git_status) == 0
		&& WIFEXITED(tar_status) && WEXITSTATUS(tar_status) == 0;
}

/* Bounded-depth search for profiledef.sh — most SYN-OS history has it
 * within 2-3 levels of the tree root (e.g. "SYN-ISO-PROFILE/profiledef.sh"
 * or "SYN-OS-V4/SYN-ISO-PROFILE/profiledef.sh"), so a shallow recursive
 * search is enough without walking an entire large tree unnecessarily. */
static bool find_profiledef(const char *dir, int depth, char *out, size_t out_len) {
	if (depth > 4) {
		return false;
	}

	char candidate[2048];
	snprintf(candidate, sizeof(candidate), "%s/profiledef.sh", dir);
	struct stat st;
	if (stat(candidate, &st) == 0) {
		snprintf(out, out_len, "%s", dir);
		return true;
	}

	DIR *d = opendir(dir);
	if (!d) {
		return false;
	}
	struct dirent *ent;
	bool found = false;
	while (!found && (ent = readdir(d))) {
		if (ent->d_name[0] == '.') {
			continue;
		}
		char child[2048];
		snprintf(child, sizeof(child), "%s/%s", dir, ent->d_name);
		struct stat child_st;
		if (stat(child, &child_st) == 0 && S_ISDIR(child_st.st_mode)) {
			found = find_profiledef(child, depth + 1, out, out_len);
		}
	}
	closedir(d);
	return found;
}

bool syn_git_extract_and_find_profile(const char *extracted_dir, const char *mirror_path,
	const char *commit_sha, char *profile_dir_out, size_t profile_dir_out_len,
	char *err_out, size_t err_out_len) {
	if (err_out && err_out_len > 0) {
		err_out[0] = '\0';
	}

	char short_sha[16];
	snprintf(short_sha, sizeof(short_sha), "%.12s", commit_sha);

	/* extracted_dir (paths->extracted — see syn_build_paths.h) lives
	 * under ~/.local/share/syn-os/iso-builder/, never inside the repo
	 * checkout, so a stale extraction from an old run is never mistaken
	 * for something the repo itself tracks. Each commit gets its own
	 * subdirectory, keyed by short SHA. */
	char dest[1024];
	snprintf(dest, sizeof(dest), "%s/%s", extracted_dir, short_sha);

	char *rm_argv[] = {"rm", "-rf", dest, NULL};
	run_cmd(rm_argv);
	char *mkdir_argv[] = {"mkdir", "-p", dest, NULL};
	char mkdir_err[512];
	if (!run_cmd_capture(mkdir_argv, mkdir_err, sizeof(mkdir_err))) {
		if (err_out) {
			snprintf(err_out, err_out_len, "Could not create %s: %s", dest, mkdir_err);
		}
		return false;
	}

	if (!archive_extract(mirror_path, commit_sha, dest)) {
		if (err_out) {
			snprintf(err_out, err_out_len, "git archive / tar extraction failed for commit %s", short_sha);
		}
		return false;
	}

	if (!find_profiledef(dest, 0, profile_dir_out, profile_dir_out_len)) {
		if (err_out) {
			snprintf(err_out, err_out_len, "No profiledef.sh found anywhere in %s's tree", short_sha);
		}
		return false;
	}
	return true;
}
