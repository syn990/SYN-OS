/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_software_build.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>

static void emit(syn_software_build_line_cb on_line, void *userdata, const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (on_line) {
		on_line(buf, userdata);
	} else {
		printf("%s\n", buf);
		fflush(stdout);
	}
}

/* fork()+execvp(), stdout+stderr redirected into `log_fd` (append mode,
 * caller keeps one fd open across the configure/build/install trio so
 * they land in the same per-tool log file in order) — never
 * system()/popen(), matching this codebase's established convention.
 *
 * setpgid(0, 0) puts the child in its own process group before exec —
 * confirmed by direct user testing: dragging/resizing the terminal
 * window during this build loop caused intermittent tool build
 * failures (a step reporting failure despite its own log showing
 * complete, successful output), something dozens of scripted
 * SIGWINCH/resize tests against cmake in isolation never reproduced.
 * The one thing scripted testing couldn't replicate is a REAL window
 * drag's actual signal-delivery characteristics through this process's
 * terminal — and since these children never draw anything or read
 * from the terminal themselves, there's no reason for them to still be
 * members of the foreground process group that terminal-generated
 * signals (SIGWINCH included) target. Removing them from that group
 * entirely closes the whole class of "some terminal signal reaches an
 * unrelated build child" regardless of which exact signal was
 * responsible — a fix that doesn't depend on having pinned the precise
 * mechanism, since that couldn't be reproduced outside a real drag to
 * confirm directly. */
static bool run_step(char *const argv[], int log_fd) {
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		setpgid(0, 0);
		dup2(log_fd, STDOUT_FILENO);
		dup2(log_fd, STDERR_FILENO);
		execvp(argv[0], argv);
		_exit(127);
	}
	int status;
	pid_t r;
	do {
		r = waitpid(pid, &status, 0);
	} while (r < 0 && errno == EINTR);
	return r >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool build_one_tool(const char *proj_dir, const char *tool_name,
	const char *scratch_build_dir, const char *profile_airootfs, const char *log_path) {
	(void)tool_name;
	int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (log_fd < 0) {
		return false;
	}

	char build_type_arg[] = "-DCMAKE_BUILD_TYPE=Release";
	char *configure_argv[] = {"cmake", "-B", (char *)scratch_build_dir, "-S", (char *)proj_dir, build_type_arg, NULL};
	bool ok = run_step(configure_argv, log_fd);

	if (ok) {
		char *build_argv[] = {"cmake", "--build", (char *)scratch_build_dir, NULL};
		ok = run_step(build_argv, log_fd);
	}

	if (ok) {
		char destdir_env[1100];
		snprintf(destdir_env, sizeof(destdir_env), "DESTDIR=%s", profile_airootfs);
		/* cmake --install has no --destdir flag — DESTDIR is an env var
		 * cmake reads, so this needs setenv, not an argv entry (matches
		 * how BUILD-ARCHISO.zsh's own `DESTDIR=... cmake --install ...`
		 * works — an env-var prefix on the command, not a CLI flag). */
		setenv("DESTDIR", profile_airootfs, 1);
		char *install_argv[] = {"cmake", "--install", (char *)scratch_build_dir, "--prefix", "/usr", NULL};
		ok = run_step(install_argv, log_fd);
		unsetenv("DESTDIR");
	}

	close(log_fd);
	return ok;
}

static void print_log_tail(const char *log_path, syn_software_build_line_cb on_line, void *userdata) {
	FILE *f = fopen(log_path, "r");
	if (!f) {
		return;
	}
	/* Keep only the last 15 lines — same "don't dump the whole cmake
	 * transcript to the screen, just enough to see what broke" balance
	 * BUILD-ARCHISO.zsh's own `tail -n 15` struck. */
	char lines[15][1024];
	int count = 0, total = 0;
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		snprintf(lines[total % 15], sizeof(lines[0]), "%s", line);
		total++;
	}
	fclose(f);
	count = total < 15 ? total : 15;
	int start = total < 15 ? 0 : total % 15;
	for (int i = 0; i < count; i++) {
		emit(on_line, userdata, "    %s", lines[(start + i) % 15]);
	}
}

bool syn_software_build_all(const char *software_dir, const char *build_root,
	const char *profile_airootfs, const char *log_dir,
	syn_software_build_line_cb on_line, void *userdata) {
	DIR *d = opendir(software_dir);
	if (!d) {
		return false;
	}

	struct dirent *ent;
	while ((ent = readdir(d))) {
		size_t nlen = strlen(ent->d_name);
		if (nlen < 5 || strcmp(ent->d_name + nlen - 4, "-src") != 0) {
			continue;
		}

		char proj_dir[1200];
		snprintf(proj_dir, sizeof(proj_dir), "%s/%s", software_dir, ent->d_name);
		struct stat st;
		if (stat(proj_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
			continue;
		}

		char tool_name[256];
		snprintf(tool_name, sizeof(tool_name), "%.*s", (int)(nlen - 4), ent->d_name);

		emit(on_line, userdata, "Building %s for the live environment...", tool_name);

		/* Built under build_root (paths->scratch — already outside the
		 * repo checkout), never as a sibling of the source directory —
		 * that used to dump a real <tool>-src-build/ folder directly
		 * into the tracked SYN-SOFTWARE/ tree on every single build,
		 * visible in a file browser regardless of .gitignore. */
		char scratch_build_dir[1200];
		snprintf(scratch_build_dir, sizeof(scratch_build_dir), "%s/software-build/%s", build_root, tool_name);
		char log_path[1200];
		snprintf(log_path, sizeof(log_path), "%s/%s.log", log_dir, tool_name);

		if (build_one_tool(proj_dir, tool_name, scratch_build_dir, profile_airootfs, log_path)) {
			emit(on_line, userdata, "%s built and staged into the live environment.", tool_name);
		} else {
			emit(on_line, userdata, "%s build failed — it won't be available on this ISO. Continuing without it.", tool_name);
			emit(on_line, userdata, "  Full log: %s", log_path);
			emit(on_line, userdata, "  Last 15 lines:");
			print_log_tail(log_path, on_line, userdata);
		}
	}
	closedir(d);

	emit(on_line, userdata, "Per-tool build logs saved under: %s", log_dir);
	return true;
}
