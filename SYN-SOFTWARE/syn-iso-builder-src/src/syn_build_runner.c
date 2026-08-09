/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_build_runner.h"
#include "syn_mount_cleanup.h"
#include "syn_software_build.h"
#include "syn_bar_tone.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <pty.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <time.h>

typedef struct {
	syn_build_line_cb on_line;
	syn_build_idle_cb on_idle;
	syn_build_provisional_cb on_provisional;
	void *userdata;
} emit_ctx;

/* Same tone meanings syn-bar-core already uses OS-wide (see its
 * RELAY_CONNECT_TONE_HZ/RELAY_FAIL_DTMF_*): a single 1900Hz tone means
 * "succeeded", the 697/1209 DTMF pair means "failed" — a build success/
 * failure should sound the same as every other success/failure in
 * SYN-OS, not invent its own vocabulary. BUILD_START has no OS-wide
 * precedent to match (nothing else "starts" a multi-minute background
 * operation the way this does), so it gets its own low, unmistakably-
 * distinct-from-both DTMF pair. */
#define BUILD_START_DTMF_LOW 697.0
#define BUILD_START_DTMF_HIGH 1477.0
#define BUILD_START_TONE_SECONDS 0.15
#define BUILD_SUCCESS_TONE_HZ 1900.0
#define BUILD_SUCCESS_TONE_SECONDS 0.2
#define BUILD_FAIL_DTMF_LOW 697.0
#define BUILD_FAIL_DTMF_HIGH 1209.0
#define BUILD_FAIL_DTMF_SECONDS 0.15

static void emit(const emit_ctx *ctx, const char *fmt, ...) {
	char buf[1200];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (ctx->on_line) {
		ctx->on_line(buf, ctx->userdata);
	} else {
		printf("%s\n", buf);
		fflush(stdout);
	}
}

static void forward_line(const char *line, void *userdata) {
	const emit_ctx *ctx = userdata;
	emit(ctx, "%s", line);
}

static bool run_simple(char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		execvp(argv[0], argv);
		_exit(127);
	}
	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool dir_exists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool file_exists(const char *path) {
	struct stat st;
	return stat(path, &st) == 0;
}

static void mkdir_p(const char *path) {
	char *argv[] = {"mkdir", "-p", (char *)path, NULL};
	run_simple(argv);
}

static void rm_rf(const char *path) {
	char *argv[] = {"rm", "-rf", (char *)path, NULL};
	run_simple(argv);
}

/* ---- Step 4: retired [community] repo fix -----------------------------
 * Arch merged [community] into [extra] in mid-2023 under the same
 * package names — a historical profile's pacman.conf built before that
 * still references the dead repo, which fails to resolve against
 * today's mirrors. Strip the [community] block (its header line and
 * every line up to the next blank line) in place. Direct port of the
 * original script's sed one-liner, as plain text processing instead. */
static void strip_community_repo(const char *pacman_conf_path, const emit_ctx *ctx) {
	FILE *f = fopen(pacman_conf_path, "r");
	if (!f) {
		return;
	}

	char *lines[4096];
	int count = 0;
	char buf[1024];
	bool found = false;
	while (count < 4096 && fgets(buf, sizeof(buf), f)) {
		if (strncmp(buf, "[community]", 11) == 0) {
			found = true;
		}
		lines[count] = strdup(buf);
		count++;
	}
	fclose(f);

	if (!found) {
		for (int i = 0; i < count; i++) free(lines[i]);
		return;
	}

	emit(ctx, "This build's pacman.conf references the retired [community] repo "
		"(merged into [extra] by Arch in mid-2023) — removing the dead repo "
		"block so the build resolves against today's mirrors.");

	f = fopen(pacman_conf_path, "w");
	if (!f) {
		for (int i = 0; i < count; i++) free(lines[i]);
		return;
	}
	bool skipping = false;
	for (int i = 0; i < count; i++) {
		if (strncmp(lines[i], "[community]", 11) == 0) {
			skipping = true;
			free(lines[i]);
			continue;
		}
		if (skipping) {
			/* blank line (just "\n") ends the block */
			if (lines[i][0] == '\n' || lines[i][0] == '\0') {
				skipping = false;
			}
			free(lines[i]);
			continue;
		}
		fputs(lines[i], f);
		free(lines[i]);
	}
	fclose(f);
}

/* ---- Step 5: grub presence check --------------------------------------
 * mkarchiso refuses to validate a profile declaring a grub bootmode
 * without grub-install present. Only relevant for a subset of
 * historical profiles — most named builds are systemd-boot/syslinux
 * only, so this is a no-op for the common case. */
static void ensure_grub_if_needed(const char *profile_dir, const emit_ctx *ctx) {
	char profiledef[1200];
	snprintf(profiledef, sizeof(profiledef), "%s/profiledef.sh", profile_dir);

	FILE *f = fopen(profiledef, "r");
	if (!f) {
		return;
	}
	bool needs_grub = false;
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "uefi.grub") || strstr(line, "bios.grub")) {
			needs_grub = true;
			break;
		}
	}
	fclose(f);
	if (!needs_grub) {
		return;
	}

	if (access("/usr/bin/grub-install", X_OK) == 0) {
		return;
	}

	emit(ctx, "This build's profiledef.sh declares a grub bootmode, but grub "
		"isn't installed on this host. Installing it now (this doesn't touch "
		"this host's own bootloader).");
	char *argv[] = {"pacman", "-Sy", "--noconfirm", "--needed", "grub", NULL};
	if (!run_simple(argv)) {
		emit(ctx, "Failed to install grub — the build may fail to validate without it.");
	}
}

/* ---- tmpfs disclosure --------------------------------------------------
 * Purely informational — scratch/ always lives on disk inside the repo
 * (see syn_build_paths.h), this never redirects into /tmp itself. Just
 * tells the user whether /tmp is tmpfs, in case they want to bind-mount
 * their own tmpfs over .syn-build/scratch for the speed-up, instead of
 * the old script silently deciding that for them. */
static void disclose_tmpfs_status(const char *scratch_dir, const emit_ctx *ctx) {
	char *argv[] = {"findmnt", "-n", "-o", "FSTYPE", "/tmp", NULL};
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execvp("findmnt", argv);
		_exit(127);
	}
	close(pipefd[1]);
	char fstype[64] = "";
	ssize_t n = read(pipefd[0], fstype, sizeof(fstype) - 1);
	fstype[n > 0 ? n : 0] = '\0';
	close(pipefd[0]);
	int status;
	waitpid(pid, &status, 0);

	size_t len = strlen(fstype);
	while (len > 0 && (fstype[len - 1] == '\n' || fstype[len - 1] == '\r')) {
		fstype[--len] = '\0';
	}

	if (strcmp(fstype, "tmpfs") == 0) {
		emit(ctx, "tmpfs detected at /tmp — building on disk at %s (not /tmp); "
			"bind-mount a tmpfs there yourself first if you want the speed-up.", scratch_dir);
	} else {
		emit(ctx, "Building on disk at %s.", scratch_dir);
	}
}

/* mkarchiso/pacman colorize their own terminal output with real ANSI
 * escape sequences (CSI: ESC '[' ... final-byte, e.g. the yellow used
 * for "checking package integrity" / warning text) — fine for a real
 * terminal, but this tool renders resolved lines as plain text via
 * mvprintw() into the ncurses panel, which has no ANSI interpreter, so
 * an unstripped escape sequence shows up as literal garbage characters
 * (confirmed live: "^[[1;33mc^[[m^[[" polluting the panel). Strips any
 * ESC '[' ... sequence (params 0-9;) up through its final letter,
 * leaving the actual readable text — the same class of "resolve raw
 * terminal control codes into plain text" this loop already does for
 * \r/\n. */
static void strip_ansi(const char *in, char *out, size_t out_len) {
	size_t oi = 0;
	for (size_t i = 0; in[i] != '\0' && oi < out_len - 1; i++) {
		if ((unsigned char)in[i] == 0x1b && in[i + 1] == '[') {
			i += 2;
			while (in[i] != '\0' && (in[i] == ';' || (in[i] >= '0' && in[i] <= '9'))) {
				i++;
			}
			/* in[i] is now the final byte (e.g. 'm', 'C', 'K') — skip it
			 * too; the outer for's i++ then advances past it. If the
			 * string ended mid-sequence, in[i] == '\0' and the loop
			 * condition above stops us correctly either way. */
			continue;
		}
		out[oi++] = in[i];
	}
	out[oi] = '\0';
}

/* ---- Step 2/6: mkarchiso itself ----------------------------------------
 * This whole tool runs as root (see main.c's re-exec under doas at
 * startup) — no doas here, just a direct exec. stdout+stderr are
 * attached to a pty (forkpty()), not a plain pipe — mkarchiso/pacman/
 * pacstrap detect whether they're on a real terminal and suppress their
 * own \r-redrawn progress/hash-check bars over a pipe (and even where
 * they don't, a plain pipe has no notion of "redraw this line in place"
 * for fgets() to represent — user-flagged gap: "it does not have the
 * hash loading bars... seems to have escaped ncurses"). forkpty()
 * allocates its OWN pty pair independent of whatever the real terminal
 * is doing — mkarchiso just needs something that satisfies isatty(),
 * not the actual physical terminal — so this works fine with ncurses
 * still fully up and owning the real terminal via stdscr (see
 * syn_tui_dashboard.c's launch_build(), which no longer calls
 * endwin() for the build). That in turn means a real terminal resize
 * is just ncurses' own KEY_RESIZE, handled wherever the caller reads
 * input next (syn_tui_buildlog_append() redraws from current
 * getmaxyx() on every line, so the panel reflows on the very next
 * output line after a resize) — no SIGWINCH handling needed here at
 * all, unlike this function's previous version which had to guard a
 * raw blocking read() against resizes of a real terminal ncurses no
 * longer owned mid-build. Read loop is byte-chunked on raw read(2),
 * not fgets() — a line is flushed to the caller on '\n' OR '\r' so an
 * in-place progress redraw still surfaces as a discrete update instead
 * of being buffered forever waiting for a '\n' that a redrawing bar
 * never sends. */
static bool run_mkarchiso(const char *scratch_dir, const char *output_dir,
	const char *profile_dir, const emit_ctx *ctx) {
	struct winsize ws;
	struct winsize *ws_ptr = NULL;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
		ws_ptr = &ws;
	}

	int master_fd;
	pid_t pid = forkpty(&master_fd, NULL, NULL, ws_ptr);
	if (pid < 0) {
		return false;
	}
	if (pid == 0) {
		execlp("mkarchiso", "mkarchiso", "-v", "-w", scratch_dir,
			"-o", output_dir, profile_dir, (char *)NULL);
		_exit(127);
	}

	char chunk[4096];
	char line[2048];
	size_t line_len = 0;
	char clean[2048];
	/* mksquashfs (and pacman's own download bars) redraw a live
	 * progress line via \r continuously for minutes at a time with no
	 * \n at all until fully done. An earlier version tried periodically
	 * emit()-ing the \r-buffered line as a new permanent panel line,
	 * which flooded the scrollback with dozens of near-duplicate lines
	 * (confirmed live: "15356/67687" repeated four times in a row) —
	 * ctx->on_provisional instead updates ONE line in place
	 * (syn_tui_buildlog_update_provisional()), same as a real terminal
	 * shows a progress bar. Still rate-limited to ~5/sec, not for
	 * flooding (in-place updates can't flood) but so a very fast
	 * redrawing bar doesn't force a full ncurses panel repaint on every
	 * single \r. The eventual real \n (or EOF) always commits the true
	 * final state as a permanent line, same as before. */
	struct timespec last_provisional = {0, 0};
	for (;;) {
		/* select() with a short timeout instead of a plain blocking
		 * read() — mkarchiso/pacstrap can go quiet for a while (a slow
		 * package download produces no output), and without this the
		 * caller's on_idle (panel resize pickup) would only ever fire
		 * on the next actual output line, leaving a resized terminal's
		 * panel stuck at its old geometry for however long the quiet
		 * stretch lasts. */
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(master_fd, &fds);
		struct timeval tv = {0, 200000}; /* 200ms */
		int ready = select(master_fd + 1, &fds, NULL, NULL, &tv);
		if (ready < 0) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		if (ready == 0) {
			if (ctx->on_idle) {
				ctx->on_idle(ctx->userdata);
			}
			continue;
		}

		ssize_t n = read(master_fd, chunk, sizeof(chunk));
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			break; /* EIO here just means the slave side closed — pty quirk, not a real error */
		}
		if (n == 0) {
			break;
		}
		for (ssize_t i = 0; i < n; i++) {
			char c = chunk[i];
			if (c == '\n') {
				/* Real end of a line — flush whatever's been
				 * accumulated (possibly redrawn several times via \r
				 * below) as the final state of that line. */
				if (line_len > 0) {
					line[line_len] = '\0';
					strip_ansi(line, clean, sizeof(clean));
					emit(ctx, "%s", clean);
					line_len = 0;
				}
			} else if (c == '\r') {
				/* Carriage return, not a line end — a progress bar
				 * redrawing in place sends \r then overwrites from
				 * column 0. Push whatever this redraw left behind to
				 * the caller's provisional-line display (rate-limited
				 * below) before rewinding — the eventual real \n (or
				 * EOF) commits the true final state as a permanent line
				 * same as before. */
				if (line_len > 0 && ctx->on_provisional) {
					struct timespec now;
					clock_gettime(CLOCK_MONOTONIC, &now);
					double elapsed = (double)(now.tv_sec - last_provisional.tv_sec)
						+ (double)(now.tv_nsec - last_provisional.tv_nsec) / 1e9;
					if (elapsed >= 0.2) {
						line[line_len] = '\0';
						strip_ansi(line, clean, sizeof(clean));
						ctx->on_provisional(clean, ctx->userdata);
						last_provisional = now;
					}
				}
				line_len = 0;
			} else if (line_len < sizeof(line) - 1) {
				line[line_len++] = c;
			}
		}
	}
	if (line_len > 0) {
		line[line_len] = '\0';
		strip_ansi(line, clean, sizeof(clean));
		emit(ctx, "%s", clean);
	}
	close(master_fd);

	int status;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* ---- Step 7: archive the finished ISO ----------------------------------
 * Finds the .iso mkarchiso dropped in output_dir, moves it to
 * out_dir/<out_name>.iso (both user-choosable via the dashboard's
 * Output row — no longer hardcoded to paths->isos/<build_id>), and
 * copies the per-tool build-log directory alongside it (scratch/ gets
 * wiped on the NEXT run, so the logs wouldn't survive otherwise). */
static bool archive_iso(const char *output_dir, const char *out_dir,
	const char *out_name, const char *software_log_dir, char *final_iso_path_out) {
	DIR *d = opendir(output_dir);
	if (!d) {
		return false;
	}
	char found_iso[1200] = "";
	struct dirent *ent;
	while ((ent = readdir(d))) {
		size_t nlen = strlen(ent->d_name);
		if (nlen > 4 && strcmp(ent->d_name + nlen - 4, ".iso") == 0) {
			snprintf(found_iso, sizeof(found_iso), "%s/%s", output_dir, ent->d_name);
			break;
		}
	}
	closedir(d);
	if (found_iso[0] == '\0') {
		return false;
	}

	mkdir_p(out_dir);
	char dest[1200];
	snprintf(dest, sizeof(dest), "%s/%s.iso", out_dir, out_name);
	if (rename(found_iso, dest) != 0) {
		/* cross-filesystem move (e.g. output_dir on a different mount) —
		 * rename(2) can't do that, fall back to cp+rm. */
		char *argv[] = {"cp", found_iso, dest, NULL};
		if (!run_simple(argv)) {
			return false;
		}
		unlink(found_iso);
	}
	snprintf(final_iso_path_out, 1200, "%s", dest);

	if (dir_exists(software_log_dir)) {
		char log_dest[1200];
		snprintf(log_dest, sizeof(log_dest), "%s/%s-build-logs", out_dir, out_name);
		rm_rf(log_dest);
		char *argv[] = {"cp", "-r", (char *)software_log_dir, log_dest, NULL};
		run_simple(argv);
	}

	return true;
}

bool syn_build_run(const syn_build_target *target, const syn_build_paths *paths,
	const char *repo_root, const char *out_dir, const char *out_name,
	syn_build_line_cb on_line, syn_build_idle_cb on_idle,
	syn_build_provisional_cb on_provisional, void *userdata, char *final_iso_path_out) {
	emit_ctx ctx = {on_line, on_idle, on_provisional, userdata};

	emit(&ctx, "Profile: %s", target->profile_dir);
	emit(&ctx, "Output:  %s/%s.iso", out_dir, out_name);
	emit(&ctx, "Build:   %s", target->target_label);
	emit(&ctx, "");
	syn_bar_tone_play_dtmf(BUILD_START_DTMF_LOW, BUILD_START_DTMF_HIGH, BUILD_START_TONE_SECONDS);

	/* Step 1: wipe+recreate scratch/output. Mount cleanup runs BEFORE
	 * the wipe, not after — a stray bind mount from a crashed prior run
	 * would make the rm -rf itself fail with "Read-only file system"
	 * otherwise, exactly the failure mode this logic exists to prevent. */
	syn_mount_cleanup(paths->scratch);
	/* This whole process runs as root (see main.c's re-exec) — the same
	 * plain rm -rf handles a fresh scratch dir or one full of root-owned
	 * leftovers from a prior mkarchiso run identically, no ownership
	 * detection or privileged-retry dance needed. */
	rm_rf(paths->scratch);
	rm_rf(paths->output);
	if (dir_exists(paths->scratch)) {
		emit(&ctx, "Could not fully remove %s (still mounted, or a permissions "
			"issue) — aborting rather than building on top of a stale tree.", paths->scratch);
		syn_bar_tone_play_dtmf(BUILD_FAIL_DTMF_LOW, BUILD_FAIL_DTMF_HIGH, BUILD_FAIL_DTMF_SECONDS);
		return false;
	}
	mkdir_p(paths->scratch);
	mkdir_p(paths->output);

	disclose_tmpfs_status(paths->scratch, &ctx);

	/* Step 3: SYN-SOFTWARE build loop (unprivileged). SYN-SOFTWARE/ is a
	 * sibling of SYN-ISO-PROFILE in the actual resolved repo checkout —
	 * NOT necessarily anywhere near target->profile_dir, which might be
	 * an extracted commit's scratch tree or an arbitrary local profile
	 * directory with no SYN-SOFTWARE/ sibling at all. repo_root is the
	 * one syn_repo_locate_resolve() found; build state (paths->root) has
	 * no relationship to it any more (see syn_build_paths.h) so it can't
	 * be derived by string-slicing paths->root the way this used to. */
	char software_dir[1200];
	snprintf(software_dir, sizeof(software_dir), "%s/SYN-SOFTWARE", repo_root);

	char profile_airootfs[1200];
	snprintf(profile_airootfs, sizeof(profile_airootfs), "%s/airootfs", target->profile_dir);

	char software_log_dir[1200];
	snprintf(software_log_dir, sizeof(software_log_dir), "%s/syn-software-build-logs", paths->scratch);
	mkdir_p(software_log_dir);

	if (dir_exists(software_dir)) {
		syn_software_build_all(software_dir, paths->scratch, profile_airootfs, software_log_dir, forward_line, &ctx);
	}

	/* Step 4: retired [community] repo fix. */
	char pacman_conf[1200];
	snprintf(pacman_conf, sizeof(pacman_conf), "%s/pacman.conf", target->profile_dir);
	if (file_exists(pacman_conf)) {
		strip_community_repo(pacman_conf, &ctx);
	}

	/* Step 5: grub presence, only if this profile needs it. */
	ensure_grub_if_needed(target->profile_dir, &ctx);

	/* Step 6: the one privileged step. */
	emit(&ctx, "");
	emit(&ctx, "Building SYN-OS ISO (mkarchiso)...");
	bool ok = run_mkarchiso(paths->scratch, paths->output, target->profile_dir, &ctx);

	if (!ok) {
		emit(&ctx, "Build failed.");
		syn_bar_tone_play_dtmf(BUILD_FAIL_DTMF_LOW, BUILD_FAIL_DTMF_HIGH, BUILD_FAIL_DTMF_SECONDS);
		return false;
	}

	/* Step 7: archive. */
	if (!archive_iso(paths->output, out_dir, out_name, software_log_dir, final_iso_path_out)) {
		emit(&ctx, "mkarchiso succeeded but no .iso was found in %s — something's wrong.", paths->output);
		syn_bar_tone_play_dtmf(BUILD_FAIL_DTMF_LOW, BUILD_FAIL_DTMF_HIGH, BUILD_FAIL_DTMF_SECONDS);
		return false;
	}

	emit(&ctx, "ISO build complete: %s", final_iso_path_out);
	syn_bar_tone_play(BUILD_SUCCESS_TONE_HZ, BUILD_SUCCESS_TONE_SECONDS);
	return true;
}
