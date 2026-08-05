/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_tui_dashboard.h"
#include "syn_theme.h"
#include "syn_tui_basic.h"
#include "syn_tui_scrollmenu.h"
#include "syn_tui_dirpicker.h"
#include "syn_tui_buildlog.h"
#include "syn_git_history.h"
#include "syn_git_extract.h"
#include "syn_build_paths.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define COLOR_PAIR_NORMAL SYN_THEME_PAIR_NORMAL
#define COLOR_PAIR_SELECTED SYN_THEME_PAIR_SELECTED
#define COLOR_PAIR_TITLE SYN_THEME_PAIR_TITLE
#define COLOR_PAIR_BORDER SYN_THEME_PAIR_BORDER
#define COLOR_PAIR_DIM SYN_THEME_PAIR_DIM
#define COLOR_PAIR_STATUSBAR SYN_THEME_PAIR_STATUSBAR
#define COLOR_PAIR_DIR SYN_THEME_PAIR_COUNT /* live/ready accent — same slot syn-crypter's dashboard uses for its Run row */
#define COLOR_PAIR_URGENT SYN_THEME_PAIR_URGENT

#define MAX_COMMITS 500

static void draw_frame(const char *title) {
	int cols = getmaxx(stdscr);
	attron(COLOR_PAIR(COLOR_PAIR_BORDER));
	box(stdscr, 0, 0);
	attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

	attron(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
	mvprintw(0, (cols - (int)strlen(title) - 2) / 2, " %s ", title);
	attroff(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
}

static void draw_statusbar(int rows, int cols, const char *hint) {
	attron(COLOR_PAIR(COLOR_PAIR_STATUSBAR));
	mvprintw(rows - 1, 1, "%-*s", cols - 2, "");
	mvprintw(rows - 1, 2, "%s", hint);
	attroff(COLOR_PAIR(COLOR_PAIR_STATUSBAR));
}

/* A titled sub-panel inside the outer frame — carves the interior into
 * distinct bordered sections instead of one flat scroll of rows, per
 * direct user complaint about the previous flat layout ("barebones
 * lines... still clonezilla"). `x`/`width` are the panel's own column
 * range (leaving room for the outer frame's own border); `y`/`height`
 * its row range. Content is drawn by the caller AFTER this, starting at
 * `y + 1, x + 2` — this only draws the box and title. */
static void draw_panel(int y, int x, int height, int width, const char *title) {
	attron(COLOR_PAIR(COLOR_PAIR_BORDER));
	for (int i = 0; i < height; i++) {
		mvhline(y + i, x, ' ', width);
	}
	mvhline(y, x, ACS_HLINE, width);
	mvhline(y + height - 1, x, ACS_HLINE, width);
	mvvline(y, x, ACS_VLINE, height);
	mvvline(y, x + width - 1, ACS_VLINE, height);
	mvaddch(y, x, ACS_ULCORNER);
	mvaddch(y, x + width - 1, ACS_URCORNER);
	mvaddch(y + height - 1, x, ACS_LLCORNER);
	mvaddch(y + height - 1, x + width - 1, ACS_LRCORNER);
	attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

	attron(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
	mvprintw(y, x + 2, " %s ", title);
	attroff(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
}

static const char *repo_source_label(syn_repo_source s) {
	switch (s) {
	case SYN_REPO_SOURCE_AUTODETECTED: return "auto-detected";
	case SYN_REPO_SOURCE_CONFIG: return "from config";
	case SYN_REPO_SOURCE_PROMPTED: return "just set";
	}
	return "";
}

static bool tmp_is_tmpfs(void) {
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		return false;
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
		execlp("findmnt", "findmnt", "-n", "-o", "FSTYPE", "/tmp", (char *)NULL);
		_exit(127);
	}
	close(pipefd[1]);
	char fstype[64] = "";
	ssize_t n = read(pipefd[0], fstype, sizeof(fstype) - 1);
	fstype[n > 0 ? n : 0] = '\0';
	close(pipefd[0]);
	int status;
	waitpid(pid, &status, 0);
	return strncmp(fstype, "tmpfs", 5) == 0;
}

/* ---- Inline status line, drawn directly onto whatever screen is
 * currently on top (the home screen, or a sub-picker mid-flow) instead
 * of a separate syn_tui_message() modal. Purely transient progress
 * ("updating mirror...", "extracting...") doesn't need a keypress to
 * acknowledge — user-flagged gap: "so many press any key to continue
 * redundant flows." Genuine errors still use syn_tui_message(), which
 * does block, since those really do need acknowledgment. */
static void draw_status(const char *text) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	attron(COLOR_PAIR(COLOR_PAIR_DIM));
	mvprintw(rows - 2, 2, "%-*s", cols - 4, "");
	mvprintw(rows - 2, 2, "%s", text);
	attroff(COLOR_PAIR(COLOR_PAIR_DIM));
	refresh();
}

/* ---- Target resolution: one function per source type, each fills
 * `target` and returns true on a real selection, or false on cancel
 * (leaving `target` untouched) — moved here from main.c's old flow_*
 * functions since the home screen is what drives them now, not a
 * separate top-level menu. */

static bool resolve_current_tree(const char *repo_root, syn_build_target *target) {
	char today[16];
	time_t now = time(NULL);
	struct tm tm_buf;
	localtime_r(&now, &tm_buf);
	strftime(today, sizeof(today), "%Y-%m-%d", &tm_buf);

	snprintf(target->profile_dir, sizeof(target->profile_dir), "%s/SYN-ISO-PROFILE", repo_root);
	snprintf(target->build_id, sizeof(target->build_id), "local-%s", today);
	snprintf(target->target_label, sizeof(target->target_label),
		"Current working tree (%s), uncommitted changes included", repo_root);
	target->kind = SYN_TARGET_CURRENT_TREE;
	snprintf(target->detail.current_tree.repo_root, sizeof(target->detail.current_tree.repo_root), "%s", repo_root);
	return true;
}

static bool resolve_local_profile(syn_build_target *target) {
	const char *home = syn_resolve_real_home();
	char start_dir[1024];
	snprintf(start_dir, sizeof(start_dir), "%s", home[0] != '\0' ? home : "/");

	char dir[1024];
	if (syn_tui_dirpicker("Browse to a directory containing profiledef.sh", start_dir, "profiledef.sh", dir, sizeof(dir)) != 0) {
		return false;
	}

	char probe[1200];
	snprintf(probe, sizeof(probe), "%s/profiledef.sh", dir);
	if (access(probe, F_OK) != 0) {
		syn_tui_message("Error", "No profiledef.sh found at that path's root.");
		return false;
	}

	char today[16];
	time_t now = time(NULL);
	struct tm tm_buf;
	localtime_r(&now, &tm_buf);
	strftime(today, sizeof(today), "%Y-%m-%d", &tm_buf);

	snprintf(target->profile_dir, sizeof(target->profile_dir), "%s", dir);
	snprintf(target->build_id, sizeof(target->build_id), "local-profile-%s", today);
	snprintf(target->target_label, sizeof(target->target_label), "Local profile directory: %s", dir);
	target->kind = SYN_TARGET_LOCAL_PROFILE;
	snprintf(target->detail.local_profile.path, sizeof(target->detail.local_profile.path), "%s", dir);
	return true;
}

static bool resolve_browse_history(syn_build_paths *paths, syn_build_target *target) {
	const char *repo_url = SYN_GIT_MAIN_URL;
	draw_status("Updating commit history mirror…");
	if (!syn_git_ensure_mirror(paths->sources, repo_url)) {
		syn_tui_message("Error", "Could not clone/update that repository. Check your network.");
		return false;
	}

	syn_git_commit *commits = malloc(sizeof(syn_git_commit) * MAX_COMMITS);
	if (!commits) {
		return false;
	}
	int count = syn_git_list_commits(paths->sources, repo_url, commits, MAX_COMMITS);
	if (count <= 0) {
		syn_tui_message("Error", "No commits found — mirror may be empty or unreadable.");
		free(commits);
		return false;
	}

	syn_scrollmenu_row *rows = malloc(sizeof(syn_scrollmenu_row) * (size_t)count);
	for (int i = 0; i < count; i++) {
		snprintf(rows[i].columns[0], sizeof(rows[i].columns[0]), "%s", commits[i].short_sha);
		snprintf(rows[i].columns[1], sizeof(rows[i].columns[1]), "%s", commits[i].date);
		snprintf(rows[i].columns[2], sizeof(rows[i].columns[2]), "%s", commits[i].author);
		snprintf(rows[i].columns[3], sizeof(rows[i].columns[3]), "%s", commits[i].subject);
		snprintf(rows[i].filter_text, sizeof(rows[i].filter_text), "%s %s %s",
			commits[i].author, commits[i].subject, commits[i].date);
	}

	syn_scrollmenu_layout layout = { .widths = {10, 12, 16, 0}, .count = 4 };
	int chosen = syn_tui_scrollmenu("Browse Commit History", rows, count, &layout);
	free(rows);
	if (chosen < 0) {
		free(commits);
		return false;
	}

	syn_git_commit selected = commits[chosen];
	free(commits);

	char mirror[1024];
	syn_git_mirror_path(mirror, sizeof(mirror), paths->sources, repo_url);

	draw_status("Extracting commit tree…");
	char profile_dir[1024];
	char extract_err[512];
	if (!syn_git_extract_and_find_profile(paths->extracted, mirror, selected.full_sha,
			profile_dir, sizeof(profile_dir), extract_err, sizeof(extract_err))) {
		syn_tui_message("Extraction Failed", extract_err[0] ? extract_err : "Unknown extraction error.");
		return false;
	}

	snprintf(target->profile_dir, sizeof(target->profile_dir), "%s", profile_dir);
	/* syn-os-<commit-date>-<short-sha> — named after the COMMIT's own
	 * date, not today's build date, so the filename reflects what
	 * history it's actually from; sha kept for a precise identifier
	 * since dates alone collide across multiple same-day commits. */
	snprintf(target->build_id, sizeof(target->build_id), "syn-os-%s-%s", selected.date, selected.short_sha);
	snprintf(target->target_label, sizeof(target->target_label), "%s — %s (%s, %s)",
		selected.short_sha, selected.subject, selected.date, selected.author);
	target->kind = SYN_TARGET_COMMIT;
	snprintf(target->detail.commit.short_sha, sizeof(target->detail.commit.short_sha), "%s", selected.short_sha);
	snprintf(target->detail.commit.subject, sizeof(target->detail.commit.subject), "%s", selected.subject);
	snprintf(target->detail.commit.author, sizeof(target->detail.commit.author), "%s", selected.author);
	snprintf(target->detail.commit.date, sizeof(target->detail.commit.date), "%s", selected.date);
	return true;
}

/* Opens the source-type picker; on a real choice, runs that source's
 * resolution flow. Returns true (and fills `target`) only when a
 * target was actually (re)selected — cancelling anywhere leaves
 * `target` untouched, so the caller's existing target survives.
 * Three real choices only — current tree / a commit / a local
 * profile.sh directory — per explicit user scope-down: "just design a
 * simple fucking builder that does profile.sh or a commit yo or a
 * local main." Named historical builds and custom repo URLs were
 * dropped, not hidden — see removed resolve_named_build()/
 * resolve_custom_repo() and syn_manifest.c/h. */
static bool pick_target(const char *repo_root, syn_build_paths *paths, syn_build_target *target) {
	const char *items[] = {
		"Current working tree",
		"Browse commit history",
		"Local profile directory",
	};
	int choice = syn_tui_menu("Set Build Target", items, 3);
	switch (choice) {
	case 0: return resolve_current_tree(repo_root, target);
	case 1: return resolve_browse_history(paths, target);
	case 2: return resolve_local_profile(target);
	default: return false; /* -1: cancelled */
	}
}

/* ---- Build launch (unchanged from the old confirm_and_build/
 * launch_build split, just relocated here since this file now owns the
 * whole session loop instead of main.c). */

static int path_has_executable(const char *name) {
	const char *path_env = getenv("PATH");
	if (!path_env) return 0;
	char path_copy[4096];
	snprintf(path_copy, sizeof(path_copy), "%s", path_env);
	char *saveptr;
	for (char *dir = strtok_r(path_copy, ":", &saveptr); dir; dir = strtok_r(NULL, ":", &saveptr)) {
		char candidate[1024];
		snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
		if (access(candidate, X_OK) == 0) return 1;
	}
	return 0;
}

/* notify-send's own stdout/stderr are inherited raw from this process
 * by default — when it fails (e.g. no D-Bus session reachable while
 * running as root, see main.c's re-exec) its error text gets written
 * straight to the real terminal underneath whatever ncurses currently
 * has drawn, completely bypassing ncurses' own screen model. Confirmed
 * live: that raw write corrupted the screen, interleaving with
 * whatever ncurses drew next since ncurses' internal shadow buffer
 * never knew those cells changed. Redirect both to /dev/null in the
 * child — a failed notification was always silently ignored by the
 * caller anyway (toast() has no return value), so there's no
 * information lost, just no longer a stray write into the terminal
 * ncurses owns. */
static void toast(const char *title, const char *body) {
	if (!path_has_executable("notify-send")) {
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			dup2(devnull, STDERR_FILENO);
			close(devnull);
		}
		execlp("notify-send", "notify-send", title, body, (char *)NULL);
		_exit(127);
	} else if (pid > 0) {
		int status;
		waitpid(pid, &status, 0);
	}
}

static void append_log_line(const char *line, void *userdata) {
	syn_tui_buildlog_append((syn_tui_buildlog *)userdata, line);
}

/* A live \r-redrawn progress bar (mksquashfs, pacman downloads) updates
 * one line in place instead of appending a new permanent line every
 * redraw — see syn_tui_buildlog_update_provisional()'s own comment for
 * why the earlier periodic-append approach was wrong. */
static void update_provisional_line(const char *line, void *userdata) {
	syn_tui_buildlog_update_provisional((syn_tui_buildlog *)userdata, line);
}

/* Called during mkarchiso's quiet stretches (see syn_build_run()'s
 * on_idle) so a terminal resize is picked up even without new output
 * to trigger a redraw — otherwise the panel would stay at its old
 * size until mkarchiso's next line, which can be a while during a
 * slow pacman download. */
static void redraw_log_on_idle(void *userdata) {
	syn_tui_buildlog_redraw((syn_tui_buildlog *)userdata);
}

static void format_size(off_t bytes, char *out, size_t out_len) {
	double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
	if (gb >= 0.1) {
		snprintf(out, out_len, "%.2f GB", gb);
	} else {
		snprintf(out, out_len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
	}
}

/* Replaces the old plain syn_tui_message(title, one_line_body) result
 * screen — direct user complaint: "it also just hard exits to a
 * terminal, no success... I wanted more flair, it just ended after the
 * build... I want it to resolve what was built." Shows the same
 * Target detail the dashboard's own Target panel renders (SHA/author/
 * date for a commit — see the ROW_TARGET column further down in
 * syn_tui_dashboard_run()) plus the output path and finished ISO's
 * real size on success, with a full success/failure color treatment
 * instead of plain COLOR_PAIR_NORMAL text. */
static void show_build_result(bool ok, const syn_build_target *target,
		const char *final_iso_path, const char *out_dir) {
	int result_pair = ok ? COLOR_PAIR_TITLE : COLOR_PAIR_URGENT;

	char size_str[32] = "";
	if (ok) {
		struct stat st;
		const char *path = final_iso_path[0] ? final_iso_path : out_dir;
		if (stat(path, &st) == 0) {
			format_size(st.st_size, size_str, sizeof(size_str));
		}
	}

	int rows, cols;
	erase();
	getmaxyx(stdscr, rows, cols);
	draw_frame(ok ? "Build Complete" : "Build Failed");

	int panel_w = (cols * 3) / 4;
	if (panel_w < 60) panel_w = cols - 4;
	int detail_lines = (target->kind == SYN_TARGET_COMMIT) ? 3 : 0;
	int panel_h = 6 + detail_lines + (ok ? 1 : 0);
	int panel_x = (cols - panel_w) / 2;
	int panel_y = (rows - panel_h) / 2;
	if (panel_y < 1) panel_y = 1;

	draw_panel(panel_y, panel_x, panel_h, panel_w, ok ? "ISO built successfully" : "Build failed");

	int ty = panel_y + 1;
	int tx = panel_x + 2;
	int label_w = panel_w - 4;

	attron(COLOR_PAIR(result_pair) | A_BOLD);
	mvprintw(ty++, tx, "%-*.*s", label_w, label_w, target->target_label);
	attroff(COLOR_PAIR(result_pair) | A_BOLD);

	if (target->kind == SYN_TARGET_COMMIT) {
		attron(COLOR_PAIR(COLOR_PAIR_DIM));
		mvprintw(ty++, tx, "SHA:  %.*s", label_w - 6, target->detail.commit.short_sha);
		mvprintw(ty++, tx, "By:   %.*s", label_w - 6, target->detail.commit.author);
		mvprintw(ty++, tx, "Date: %.*s", label_w - 6, target->detail.commit.date);
		attroff(COLOR_PAIR(COLOR_PAIR_DIM));
	}
	ty++;

	attron(COLOR_PAIR(COLOR_PAIR_DIM));
	mvprintw(ty++, tx, "Output:");
	attroff(COLOR_PAIR(COLOR_PAIR_DIM));
	attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
	mvprintw(ty++, tx, "%-*.*s", label_w, label_w, final_iso_path[0] ? final_iso_path : out_dir);
	attroff(COLOR_PAIR(COLOR_PAIR_NORMAL));

	if (ok && size_str[0]) {
		attron(COLOR_PAIR(COLOR_PAIR_DIM));
		mvprintw(ty++, tx, "Size: %s", size_str);
		attroff(COLOR_PAIR(COLOR_PAIR_DIM));
	}

	draw_statusbar(rows, cols, "Press any key to continue...");
	refresh();
	getch();
}

/* Runs the build inside a centered, bordered scrolling panel — ncurses
 * stays up the whole time (no endwin()/syn_tui_init() round-trip like
 * this used to do), so a terminal resize mid-build is just ncurses'
 * own KEY_RESIZE handling instead of corrupting a plain scrolling
 * terminal stream. mkarchiso's own pty (forkpty(), see
 * syn_build_runner.c) is independent of the real terminal, so it
 * doesn't need ncurses out of the way — see run_mkarchiso()'s comment
 * for why. */
static void launch_build(const syn_build_target *target, syn_build_paths *paths, const char *repo_root,
		const char *out_dir, const char *out_name) {
	syn_tui_buildlog *log = syn_tui_buildlog_open("Build Output");
	syn_tui_buildlog_append(log, "--- syn-iso-builder: starting build ---");
	syn_tui_buildlog_append(log, "");

	toast("SYN-OS ISO Builder", "Build starting…");
	char final_iso_path[1200] = "";
	bool ok = syn_build_run(target, paths, repo_root, out_dir, out_name, append_log_line, redraw_log_on_idle, update_provisional_line, log, final_iso_path);
	toast("SYN-OS ISO Builder", ok ? "Build finished successfully." : "Build failed — see log above.");

	syn_tui_buildlog_close(log);
	show_build_result(ok, target, final_iso_path, out_dir);
}

/* ---- The home screen itself ------------------------------------------- */

typedef enum {
	ROW_TARGET,
	ROW_SCRATCH,
	ROW_OUTPUT,
	ROW_LAUNCH,
	ROW_QUIT,
	ROW_COUNT,
} dashboard_row;

/* Opens a directory-then-filename picker for the output ISO. Directory
 * defaults to (and starts browsing from) whatever's currently set;
 * filename prompt pre-fills the current name so Enter-through-both
 * keeps today's value unchanged. Only overwrites `out_dir`/`out_name` on
 * a full confirm — cancelling either step leaves both untouched. Returns
 * true only on a real confirm — the caller uses this to decide whether
 * to mark the filename as user-customized; treating a mere Esc-cancel
 * as a customization was a real bug (opening then backing out of this
 * picker permanently stopped the filename from following a newly
 * picked Target, since nothing had actually changed). */
static bool pick_output(char *out_dir, size_t out_dir_len, char *out_name, size_t out_name_len) {
	char picked_dir[1024];
	if (syn_tui_dirpicker("Browse to a directory for the finished ISO", out_dir, NULL, picked_dir, sizeof(picked_dir)) != 0) {
		return false;
	}
	char picked_name[160];
	if (syn_tui_text_prompt("ISO filename (without .iso)", out_name, picked_name, sizeof(picked_name)) != 0
			|| picked_name[0] == '\0') {
		return false;
	}
	snprintf(out_dir, out_dir_len, "%s", picked_dir);
	snprintf(out_name, out_name_len, "%s", picked_name);
	return true;
}

void syn_tui_dashboard_run(syn_build_paths *paths, const char *repo_root, syn_repo_source repo_source) {
	syn_build_target target;
	resolve_current_tree(repo_root, &target); /* sensible default — matches today's implicit "current tree" start */

	bool tmpfs_available = tmp_is_tmpfs();
	bool use_tmpfs = tmpfs_available;

	char disk_scratch[1024];
	snprintf(disk_scratch, sizeof(disk_scratch), "%s", paths->scratch);
	char tmpfs_scratch[1024] = "/tmp/synos-build-scratch";

	/* Output directory/filename — editable, defaults to paths->isos/
	 * <build_id>.iso like every build so far this session, but nothing
	 * stopped a user from wanting either changed until now (direct
	 * complaint: "no way to adjust the output directory or name nothing
	 * done there at all"). Filename only re-syncs to the target's own
	 * build_id automatically while the user hasn't customized it — once
	 * they pick a name via pick_output(), a newly picked Target won't
	 * silently overwrite their choice. */
	char out_dir[1024];
	snprintf(out_dir, sizeof(out_dir), "%s", paths->isos);
	char out_name[160];
	snprintf(out_name, sizeof(out_name), "%s", target.build_id);
	bool out_name_customized = false;

	dashboard_row cursor = ROW_TARGET;
	int rows, cols;

	while (1) {
		snprintf(paths->scratch, sizeof(paths->scratch), "%s",
			use_tmpfs ? tmpfs_scratch : disk_scratch);

		/* Keep the filename following the Target's own build_id until
		 * the user explicitly customizes it via pick_output() — after
		 * that, their choice sticks even if Target changes again. */
		if (!out_name_customized) {
			snprintf(out_name, sizeof(out_name), "%s", target.build_id);
		}

		char iso_dest[1200];
		snprintf(iso_dest, sizeof(iso_dest), "%s/%s.iso", out_dir, out_name);

		erase();
		getmaxyx(stdscr, rows, cols);
		draw_frame("SYN-ISO-BUILDER");

		/* Three side-by-side columns instead of stacked full-width
		 * panels — direct user instruction: "I want a full menu with
		 * two or three columns not list wiggly lists." Target | Repo &
		 * Scratch | Output, all starting at the same row, each its own
		 * vertical slice of the screen. */
		int y = 1;
		int gap = 1;
		int col_w = (cols - 4 - 2 * gap) / 3;
		int col1_x = 2;
		int col2_x = col1_x + col_w + gap;
		int col3_x = col2_x + col_w + gap;

		int target_detail_lines;
		switch (target.kind) {
		case SYN_TARGET_COMMIT: target_detail_lines = 4; break; /* SHA, Subject, Author, Date */
		default: target_detail_lines = 0; break;                /* current tree / local profile: label says it all */
		}
		int target_panel_h = 3 + target_detail_lines;

		int config_panel_h = 2 + (tmpfs_available ? 3 : 2) + 2;

		int output_panel_h = 4;

		int tallest = target_panel_h;
		if (config_panel_h > tallest) tallest = config_panel_h;
		if (output_panel_h > tallest) tallest = output_panel_h;

		/* --- Column 1: Target --- */
		draw_panel(y, col1_x, tallest, col_w, "Target");
		{
			int ty = y + 1;
			bool target_has_cursor = (cursor == ROW_TARGET);
			attron(COLOR_PAIR(target_has_cursor ? COLOR_PAIR_SELECTED : COLOR_PAIR_NORMAL) | A_BOLD);
			mvprintw(ty++, col1_x + 2, "%s%.*s%s",
				target_has_cursor ? "< " : "  ", col_w - 6, target.target_label, target_has_cursor ? " >" : "");
			attroff(COLOR_PAIR(target_has_cursor ? COLOR_PAIR_SELECTED : COLOR_PAIR_NORMAL) | A_BOLD);

			switch (target.kind) {
			case SYN_TARGET_COMMIT:
				attron(COLOR_PAIR(COLOR_PAIR_DIM));
				mvprintw(ty++, col1_x + 2, "SHA:  %.*s", col_w - 10, target.detail.commit.short_sha);
				mvprintw(ty++, col1_x + 2, "By:   %.*s", col_w - 10, target.detail.commit.author);
				mvprintw(ty++, col1_x + 2, "Date: %.*s", col_w - 10, target.detail.commit.date);
				attroff(COLOR_PAIR(COLOR_PAIR_DIM));
				break;
			default:
				break;
			}
		}

		/* --- Column 2: Repo checkout & Scratch space --- */
		draw_panel(y, col2_x, tallest, col_w, "Build Config");
		{
			int cy = y + 1;
			attron(COLOR_PAIR(COLOR_PAIR_DIM));
			mvprintw(cy++, col2_x + 2, "Repo (%s):", repo_source_label(repo_source));
			attroff(COLOR_PAIR(COLOR_PAIR_DIM));
			attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
			mvprintw(cy++, col2_x + 2, "%.*s", col_w - 4, repo_root);
			attroff(COLOR_PAIR(COLOR_PAIR_NORMAL));

			/* Scratch: unchanged from the earlier per-session rework —
			 * the proven < value > cyclable pattern, only shown as
			 * choosable when tmpfs is genuinely available. */
			bool scratch_has_cursor = (cursor == ROW_SCRATCH) && tmpfs_available;
			attron(COLOR_PAIR(COLOR_PAIR_DIM));
			mvprintw(cy++, col2_x + 2, "Scratch:");
			attroff(COLOR_PAIR(COLOR_PAIR_DIM));
			if (tmpfs_available) {
				attron(COLOR_PAIR(scratch_has_cursor ? COLOR_PAIR_SELECTED : COLOR_PAIR_NORMAL) | (scratch_has_cursor ? A_BOLD : 0));
				mvprintw(cy++, col2_x + 2, "%s%.*s%s",
					scratch_has_cursor ? "< " : "  ",
					col_w - 6, use_tmpfs ? "tmpfs (fast)" : "disk (slow)",
					scratch_has_cursor ? " >" : "");
				attroff(COLOR_PAIR(scratch_has_cursor ? COLOR_PAIR_SELECTED : COLOR_PAIR_NORMAL) | (scratch_has_cursor ? A_BOLD : 0));
				attron(COLOR_PAIR(COLOR_PAIR_DIM));
				mvprintw(cy++, col2_x + 2, "%.*s", col_w - 4, paths->scratch);
				attroff(COLOR_PAIR(COLOR_PAIR_DIM));
			} else {
				attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
				mvprintw(cy++, col2_x + 2, "%.*s", col_w - 4, paths->scratch);
				attroff(COLOR_PAIR(COLOR_PAIR_NORMAL));
				attron(COLOR_PAIR(COLOR_PAIR_DIM));
				mvprintw(cy++, col2_x + 2, "(no tmpfs)");
				attroff(COLOR_PAIR(COLOR_PAIR_DIM));
			}
		}

		/* --- Column 3: Output --- */
		draw_panel(y, col3_x, tallest, col_w, "Output");
		{
			int oy = y + 1;
			bool output_has_cursor = (cursor == ROW_OUTPUT);
			attron(COLOR_PAIR(COLOR_PAIR_DIM));
			mvprintw(oy++, col3_x + 2, "Directory:");
			attroff(COLOR_PAIR(COLOR_PAIR_DIM));
			attron(COLOR_PAIR(output_has_cursor ? COLOR_PAIR_SELECTED : COLOR_PAIR_NORMAL) | (output_has_cursor ? A_BOLD : 0));
			mvprintw(oy++, col3_x + 2, "%s%.*s%s",
				output_has_cursor ? "< " : "  ", col_w - 6, out_dir, output_has_cursor ? " >" : "");
			attroff(COLOR_PAIR(output_has_cursor ? COLOR_PAIR_SELECTED : COLOR_PAIR_NORMAL) | (output_has_cursor ? A_BOLD : 0));
			attron(COLOR_PAIR(COLOR_PAIR_DIM));
			mvprintw(oy++, col3_x + 2, "Filename:");
			attroff(COLOR_PAIR(COLOR_PAIR_DIM));
			attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
			mvprintw(oy++, col3_x + 2, "%.*s.iso", col_w - 8, out_name);
			attroff(COLOR_PAIR(COLOR_PAIR_NORMAL));
		}

		y += tallest + 2;

		/* Action rows stay outside any panel — visually distinct as
		 * "you're done configuring, do it" rather than another fact. */
		const char *actions[2] = {"Launch Build", "Quit"};
		dashboard_row action_rows[2] = {ROW_LAUNCH, ROW_QUIT};
		for (int i = 0; i < 2; i++) {
			bool is_cursor = (cursor == action_rows[i]);
			int color = is_cursor ? COLOR_PAIR_SELECTED : (action_rows[i] == ROW_LAUNCH ? COLOR_PAIR_DIR : COLOR_PAIR_NORMAL);
			attron(COLOR_PAIR(color) | (is_cursor ? A_BOLD : 0));
			mvprintw(y + i, col1_x + 2, " %s ", actions[i]);
			attroff(COLOR_PAIR(color) | (is_cursor ? A_BOLD : 0));
		}

		draw_statusbar(rows, cols,
			"Up/Down move between fields (jumps across columns)   Enter/Left/Right change or open   Esc/q on Quit to exit");
		refresh();

		int ch = getch();
		switch (ch) {
		case KEY_UP:
		case 'k':
			do {
				cursor = (cursor - 1 + ROW_COUNT) % ROW_COUNT;
			} while (cursor == ROW_SCRATCH && !tmpfs_available);
			break;
		case KEY_DOWN:
		case 'j':
			do {
				cursor = (cursor + 1) % ROW_COUNT;
			} while (cursor == ROW_SCRATCH && !tmpfs_available);
			break;
		case KEY_LEFT:
		case KEY_RIGHT:
		case 'h':
		case 'l':
			if (cursor == ROW_SCRATCH && tmpfs_available) {
				use_tmpfs = !use_tmpfs;
			} else if (cursor == ROW_TARGET) {
				syn_build_target picked;
				if (pick_target(repo_root, paths, &picked)) {
					target = picked;
				}
			} else if (cursor == ROW_OUTPUT) {
				if (pick_output(out_dir, sizeof(out_dir), out_name, sizeof(out_name))) {
					out_name_customized = true;
				}
			}
			break;
		case 27:
		case 'q':
			if (cursor == ROW_QUIT) {
				return;
			}
			break;
		case '\n':
		case KEY_ENTER:
			if (cursor == ROW_TARGET) {
				syn_build_target picked;
				if (pick_target(repo_root, paths, &picked)) {
					target = picked;
				}
				break;
			}
			if (cursor == ROW_SCRATCH && tmpfs_available) {
				use_tmpfs = !use_tmpfs;
				break;
			}
			if (cursor == ROW_OUTPUT) {
				if (pick_output(out_dir, sizeof(out_dir), out_name, sizeof(out_name))) {
					out_name_customized = true;
				}
				break;
			}
			if (cursor == ROW_LAUNCH) {
				launch_build(&target, paths, repo_root, out_dir, out_name);
				break;
			}
			if (cursor == ROW_QUIT) {
				return;
			}
			break;
		default:
			break;
		}
	}
}
