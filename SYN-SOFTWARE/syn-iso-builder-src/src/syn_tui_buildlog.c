/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_tui_buildlog.h"
#include "syn_tui_basic.h"
#include "syn_theme.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define COLOR_PAIR_NORMAL SYN_THEME_PAIR_NORMAL
#define COLOR_PAIR_TITLE SYN_THEME_PAIR_TITLE
#define COLOR_PAIR_BORDER SYN_THEME_PAIR_BORDER
#define COLOR_PAIR_DIM SYN_THEME_PAIR_DIM
#define COLOR_PAIR_STATUSBAR SYN_THEME_PAIR_STATUSBAR
#define COLOR_PAIR_URGENT SYN_THEME_PAIR_URGENT

/* Caps retained lines the same way print_log_tail() (syn_software_build.c)
 * already caps a failure summary at 15 — here it's the whole build's
 * scrollback, not a summary, so it's generous (2000) rather than tiny,
 * but still bounded: an unbounded buffer for a build that can run for
 * many minutes with continuous pacman/pacstrap output isn't needed for
 * "what's visible right now plus a bit of recent scrollback." */
#define MAX_LINES 2000
#define LINE_WIDTH 512

struct syn_tui_buildlog {
	char title[128];
	char lines[MAX_LINES][LINE_WIDTH];
	int count;      /* how many of `lines` are populated (caps at MAX_LINES) */
	int next_slot;  /* ring-buffer write position once count == MAX_LINES */

	/* A \r-redrawn progress bar (mksquashfs, pacman downloads) updates
	 * this ONE slot in place instead of appending a new permanent line
	 * every redraw — the periodic-snapshot approach tried first flooded
	 * the scrollback with dozens of near-duplicate lines (confirmed
	 * live: "15356/67687" repeated four times in a row). Rendered as
	 * one extra row after the committed lines[], never entering the
	 * ring buffer itself. Cleared the moment a real line is committed
	 * (syn_tui_buildlog_append()) since \n always means "this line is
	 * now final," matching run_mkarchiso()'s own \r/\n handling. */
	char provisional[LINE_WIDTH];
	bool has_provisional;
};

static void draw_frame(const char *title) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	(void)rows;
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

/* Plain substring/prefix checks — same "no regex library" convention
 * syn_tui_scrollmenu.c's row_matches_filter() already established for
 * its own text matching. Classifies a line by what it already says,
 * not any new tagging threaded through emit_ctx/syn_build_line_cb —
 * every line here is exactly the same text this tool already emits
 * today, just colored by content instead of always flat white. */
typedef enum {
	LINE_KIND_NORMAL,
	LINE_KIND_SECTION,
	LINE_KIND_SUCCESS,
	LINE_KIND_FAILURE,
} line_kind;

static line_kind classify_line(const char *line) {
	if (strstr(line, "build failed") || strncmp(line, "Error:", 6) == 0
			|| strncmp(line, "CMake Error", 11) == 0) {
		return LINE_KIND_FAILURE;
	}
	if (strstr(line, "built and staged")) {
		return LINE_KIND_SUCCESS;
	}
	if (strncmp(line, "Building ", 9) == 0 || strncmp(line, "--- syn-iso-builder", 19) == 0) {
		return LINE_KIND_SECTION;
	}
	return LINE_KIND_NORMAL;
}

static int pair_for_kind(line_kind kind) {
	switch (kind) {
	case LINE_KIND_FAILURE: return COLOR_PAIR_URGENT;
	case LINE_KIND_SUCCESS: return COLOR_PAIR_TITLE; /* accent — theme's "good/ready" color elsewhere too */
	case LINE_KIND_SECTION: return COLOR_PAIR_TITLE;
	default: return COLOR_PAIR_NORMAL;
	}
}

/* Same centered-box math syn_tui_message() (syn_tui_basic.c) already
 * uses, sized generously (90% width, most of the height) rather than a
 * fixed size — a build's real output lines (paths, package names) run
 * long, and this is the only thing on screen while it's up. */
static void panel_geometry(int *y, int *x, int *h, int *w) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	*w = (cols * 9) / 10;
	*h = (rows * 9) / 10;
	*x = (cols - *w) / 2;
	*y = (rows - *h) / 2;
}

syn_tui_buildlog *syn_tui_buildlog_open(const char *title) {
	syn_tui_buildlog *log = calloc(1, sizeof(*log));
	if (!log) {
		return NULL;
	}
	snprintf(log->title, sizeof(log->title), "%s", title);
	syn_tui_buildlog_redraw(log);
	return log;
}

void syn_tui_buildlog_append(syn_tui_buildlog *log, const char *line) {
	if (!log) {
		return;
	}
	int slot = (log->count < MAX_LINES) ? log->count : log->next_slot;
	snprintf(log->lines[slot], LINE_WIDTH, "%s", line);
	if (log->count < MAX_LINES) {
		log->count++;
	} else {
		log->next_slot = (log->next_slot + 1) % MAX_LINES;
	}
	log->has_provisional = false;
	syn_tui_buildlog_redraw(log);
}

void syn_tui_buildlog_update_provisional(syn_tui_buildlog *log, const char *line) {
	if (!log) {
		return;
	}
	snprintf(log->provisional, LINE_WIDTH, "%s", line);
	log->has_provisional = true;
	syn_tui_buildlog_redraw(log);
}

void syn_tui_buildlog_redraw(syn_tui_buildlog *log) {
	if (!log) {
		return;
	}

	syn_tui_sync_term_size();

	int rows, cols;
	erase();
	getmaxyx(stdscr, rows, cols);
	draw_frame("SYN-ISO-BUILDER");

	int py, px, ph, pw;
	panel_geometry(&py, &px, &ph, &pw);

	attron(COLOR_PAIR(COLOR_PAIR_BORDER));
	for (int i = 0; i < ph; i++) {
		mvhline(py + i, px, ' ', pw);
	}
	mvhline(py, px, ACS_HLINE, pw);
	mvhline(py + ph - 1, px, ACS_HLINE, pw);
	mvvline(py, px, ACS_VLINE, ph);
	mvvline(py, px + pw - 1, ACS_VLINE, ph);
	mvaddch(py, px, ACS_ULCORNER);
	mvaddch(py, px + pw - 1, ACS_URCORNER);
	mvaddch(py + ph - 1, px, ACS_LLCORNER);
	mvaddch(py + ph - 1, px + pw - 1, ACS_LRCORNER);
	attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

	attron(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
	mvprintw(py, px + 2, " %s ", log->title);
	attroff(COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);

	int content_h = ph - 2;
	int content_w = pw - 4;
	if (content_h > 0 && content_w > 0) {
		/* Reserve the bottom row for the provisional (\r-in-progress)
		 * line when there is one, so it always renders right after the
		 * last committed line rather than being pushed off-screen or
		 * overlapping it. */
		int committed_h = log->has_provisional ? content_h - 1 : content_h;
		if (committed_h < 0) {
			committed_h = 0;
		}

		/* Oldest-to-newest, always showing the tail — the ring buffer's
		 * logical order starts at next_slot once it's wrapped, matches
		 * how syn_tui_scrollmenu's visible[] index remap already treats
		 * "logical position" as distinct from "storage slot". */
		int start = (log->count < committed_h) ? 0 : log->count - committed_h;
		int row = 0;
		for (int i = start; i < log->count; i++) {
			int slot = (log->count < MAX_LINES) ? i : (log->next_slot + i) % MAX_LINES;
			line_kind kind = classify_line(log->lines[slot]);
			int pair = pair_for_kind(kind);
			bool bold = (kind == LINE_KIND_SECTION || kind == LINE_KIND_FAILURE);
			attron(COLOR_PAIR(pair) | (bold ? A_BOLD : 0));
			mvprintw(py + 1 + row, px + 2, "%-*.*s", content_w, content_w, log->lines[slot]);
			attroff(COLOR_PAIR(pair) | (bold ? A_BOLD : 0));
			row++;
		}
		if (log->has_provisional) {
			attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
			mvprintw(py + 1 + row, px + 2, "%-*.*s", content_w, content_w, log->provisional);
			attroff(COLOR_PAIR(COLOR_PAIR_NORMAL));
		}
	}

	draw_statusbar(rows, cols, "Build in progress — this may take a while...");
	refresh();
}

void syn_tui_buildlog_close(syn_tui_buildlog *log) {
	free(log);
}
