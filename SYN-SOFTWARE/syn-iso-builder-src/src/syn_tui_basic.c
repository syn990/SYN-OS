/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_tui_basic.h"
#include "syn_theme.h"

#include <ncurses.h>
#include <locale.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define COLOR_PAIR_NORMAL SYN_THEME_PAIR_NORMAL
#define COLOR_PAIR_SELECTED SYN_THEME_PAIR_SELECTED
#define COLOR_PAIR_TITLE SYN_THEME_PAIR_TITLE
#define COLOR_PAIR_BORDER SYN_THEME_PAIR_BORDER
#define COLOR_PAIR_STATUSBAR SYN_THEME_PAIR_STATUSBAR

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

static void draw_statusbar(int rows, int cols, const char *hint, const char *info) {
	attron(COLOR_PAIR(COLOR_PAIR_STATUSBAR));
	mvprintw(rows - 1, 1, "%-*s", cols - 2, "");
	mvprintw(rows - 1, 2, "%s", hint);
	if (info && info[0]) {
		mvprintw(rows - 1, cols - 2 - (int)strlen(info), "%s", info);
	}
	attroff(COLOR_PAIR(COLOR_PAIR_STATUSBAR));
}

/* A titled sub-panel, boxed like syn_tui_dashboard's own panels — this
 * menu used to be a completely flat, borderless list, which is why
 * navigating INTO it from the boxed home screen read as "no different,
 * just further away." Matches the same box()-carving approach. */
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

static void apply_theme_colors(void) {
	syn_palette pal;
	syn_theme_load(&pal);
	start_color();
	syn_theme_apply_curses_colors();
	(void)pal;
	/* SYN_THEME_PAIR_COUNT is documented as a free slot for a tool's own
	 * extra pair — syn-crypter-src's own dashboard already sets this up
	 * (init_pair(COLOR_PAIR_DIR, 18, -1), accent color) for exactly the
	 * same "ready/accent" role this tool's Launch Build row and
	 * directory-picker listing use it for. This tool never actually
	 * called init_pair() on it — every use of COLOR_PAIR_DIR
	 * (syn_tui_dashboard.c's Launch row, syn_tui_dirpicker.c's directory
	 * listing) was rendering with ncurses' fallback for an
	 * uninitialized pair, which on some terminals is indistinguishable
	 * from invisible. */
	init_pair(SYN_THEME_PAIR_COUNT, 18, -1);
}

void syn_tui_init(void) {
	setlocale(LC_ALL, "");
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	if (has_colors()) {
		apply_theme_colors();
	}
	refresh();
}

void syn_tui_end(void) {
	endwin();
}

void syn_tui_sync_term_size(void) {
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) {
		return;
	}
	if (is_term_resized(ws.ws_row, ws.ws_col)) {
		/* resizeterm() alone left stale/garbled content on screen when
		 * tested live (resizing mid-build, during syn_tui_buildlog's
		 * select()-driven redraw loop, which never calls into
		 * ncurses' own input-driven resize detection) — old and new
		 * frame content overlapped instead of one cleanly replacing
		 * the other, and the garbling persisted into whatever screen
		 * drew next. A full endwin()+refresh() cycle forces ncurses to
		 * reinitialize its terminal state from scratch against the
		 * real current size instead of trying to reconcile against
		 * what it thought the size still was, which is what actually
		 * fixed it. */
		endwin();
		refresh();
		resizeterm(ws.ws_row, ws.ws_col);
		wclear(curscr);
	}
}

int syn_tui_menu(const char *title, const char *const *items, int count) {
	int selected = 0;
	int rows, cols;

	while (1) {
		erase();
		getmaxyx(stdscr, rows, cols);
		draw_frame(title);

		int panel_x = 2, panel_w = cols - 4;
		int panel_h = count + 2; /* border + one row per item */
		if (panel_h > rows - 3) {
			panel_h = rows - 3;
		}
		draw_panel(1, panel_x, panel_h, panel_w, "Options");

		int start_row = 2;
		int list_height = panel_h - 2;
		for (int i = 0; i < count && i < list_height; i++) {
			bool is_sel = (i == selected);
			attron(COLOR_PAIR(is_sel ? COLOR_PAIR_SELECTED : COLOR_PAIR_NORMAL) | (is_sel ? A_BOLD : 0));
			mvprintw(start_row + i, panel_x + 2, "%s%-*s",
				is_sel ? "> " : "  ", panel_w - 6, items[i]);
			attroff(COLOR_PAIR(is_sel ? COLOR_PAIR_SELECTED : COLOR_PAIR_NORMAL) | (is_sel ? A_BOLD : 0));
		}

		draw_statusbar(rows, cols, "Up/Down j/k move   Enter select   Esc/q cancel", NULL);
		refresh();

		int ch = getch();
		switch (ch) {
		case KEY_UP:
		case 'k':
			selected = (selected - 1 + count) % count;
			break;
		case KEY_DOWN:
		case 'j':
			selected = (selected + 1) % count;
			break;
		case 27:
		case 'q':
			return -1;
		case '\n':
		case KEY_ENTER:
			return selected;
		default:
			break;
		}
	}
}

void syn_tui_message(const char *title, const char *body) {
	int rows, cols;
	syn_tui_sync_term_size();
	erase();
	getmaxyx(stdscr, rows, cols);
	draw_frame(title);

	attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
	mvprintw(rows / 2, (cols - (int)strlen(body)) / 2, "%s", body);
	attroff(COLOR_PAIR(COLOR_PAIR_NORMAL));
	draw_statusbar(rows, cols, "Press any key to continue...", NULL);
	refresh();
	getch();
}

int syn_tui_text_prompt(const char *title, const char *initial, char *out, size_t out_len) {
	char buf[1024] = {0};
	if (initial) {
		snprintf(buf, sizeof(buf), "%s", initial);
	}
	size_t len = strlen(buf);
	int rows, cols;

	curs_set(1);
	while (1) {
		erase();
		getmaxyx(stdscr, rows, cols);
		draw_frame(title);

		attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
		mvprintw(rows / 2, 2, "> %s", buf);
		attroff(COLOR_PAIR(COLOR_PAIR_NORMAL));
		draw_statusbar(rows, cols, "Enter confirm   Esc cancel   Backspace delete", NULL);
		move(rows / 2, 4 + (int)len);
		refresh();

		int ch = getch();
		if (ch == '\n' || ch == KEY_ENTER) {
			curs_set(0);
			snprintf(out, out_len, "%s", buf);
			return 0;
		} else if (ch == 27) {
			curs_set(0);
			return -1;
		} else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (len > 0) {
				buf[--len] = '\0';
			}
		} else if (ch >= 32 && ch < 127 && len < sizeof(buf) - 1) {
			buf[len++] = (char)ch;
			buf[len] = '\0';
		}
	}
}
