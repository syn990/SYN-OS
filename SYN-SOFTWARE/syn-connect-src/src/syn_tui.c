/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WIFI (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_wifi_tui.h"
#include "syn_theme.h"

#include <ncurses.h>
#include <string.h>
#include <locale.h>
#include <langinfo.h>

#define P_NORMAL SYN_THEME_PAIR_NORMAL
#define P_SELECTED SYN_THEME_PAIR_SELECTED
#define P_TITLE SYN_THEME_PAIR_TITLE
#define P_BORDER SYN_THEME_PAIR_BORDER
#define P_DIM SYN_THEME_PAIR_DIM
#define P_STATUSBAR SYN_THEME_PAIR_STATUSBAR
#define P_ACCENT SYN_THEME_PAIR_COUNT /* slot 18 (theme accent) is already loaded by syn_theme_apply_curses_colors() */

void syn_wifi_tui_init(void) {
	/* doas strips LANG/LC_* from the environment before exec'ing the
	 * target program (confirmed: `doas env | grep LANG` returns nothing,
	 * even though the invoking shell has LANG=en_GB.UTF-8) — and this
	 * runs under `foot -e doas /usr/lib/syn-os/syn-wifi`, so
	 * setlocale(LC_ALL, "") silently resolves to the "C" locale, not
	 * whatever the terminal actually is. That breaks UTF-8 rendering for
	 * every box-drawing/glyph character this TUI draws. C.utf8 is a
	 * portable fallback present on any glibc system, unlike a specific
	 * language locale (en_GB.utf8 won't exist on every install) — force
	 * it explicitly whenever the resolved locale isn't already UTF-8. */
	setlocale(LC_ALL, "");
	if (strcmp(nl_langinfo(CODESET), "UTF-8") != 0) {
		setlocale(LC_ALL, "C.utf8");
	}
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	if (has_colors()) {
		start_color();
		syn_theme_apply_curses_colors();
		init_pair(P_ACCENT, 18, -1); /* theme accent, wifi-specific (e.g. security label) */
	}
	refresh();
}

void syn_wifi_tui_end(void) { endwin(); }

static void draw_frame(const char *title) {
	int cols = getmaxx(stdscr);
	attron(COLOR_PAIR(P_BORDER));
	box(stdscr, 0, 0);
	attroff(COLOR_PAIR(P_BORDER));
	attron(COLOR_PAIR(P_TITLE) | A_BOLD);
	mvprintw(0, (cols - (int)strlen(title) - 2) / 2, " %s ", title);
	attroff(COLOR_PAIR(P_TITLE) | A_BOLD);
}

static void draw_statusbar(int rows, int cols, const char *hint, const char *info) {
	attron(COLOR_PAIR(P_STATUSBAR));
	mvprintw(rows - 1, 1, "%-*s", cols - 2, "");
	mvprintw(rows - 1, 2, "%s", hint);
	if (info && info[0]) {
		mvprintw(rows - 1, cols - 2 - (int)strlen(info), "%s", info);
	}
	attroff(COLOR_PAIR(P_STATUSBAR));
}

static const char *security_label(const char *type) {
	if (strcmp(type, "open") == 0) return "open";
	if (strcmp(type, "psk") == 0) return "WPA";
	if (strcmp(type, "8021x") == 0) return "802.1x";
	return type;
}

/* 4-cell bar, each cell either filled or empty depending on `bars` (0-4). */
static void signal_glyph(int bars, char *out /* needs 13 bytes: 4 x 3-byte UTF-8 cell + NUL */) {
	out[0] = '\0';
	for (int i = 0; i < 4; i++) {
		strcat(out, i < bars ? "█" : "░");
	}
}

int syn_wifi_tui_network_list(const syn_iwd_network *networks, int count, int scanning) {
	int selected = 0, scroll_top = 0;

	while (1) {
		erase();
		int rows = getmaxy(stdscr), cols = getmaxx(stdscr);
		draw_frame("SYN-OS Wi-Fi");

		int list_rows = rows - 3;
		if (selected < scroll_top) scroll_top = selected;
		if (selected >= scroll_top + list_rows) scroll_top = selected - list_rows + 1;

		for (int row = 0; row < list_rows && scroll_top + row < count; row++) {
			int i = scroll_top + row;
			const syn_iwd_network *n = &networks[i];
			int is_sel = (i == selected);
			int color = is_sel ? P_SELECTED : P_NORMAL;

			attron(COLOR_PAIR(color));
			mvprintw(2 + row, 1, "%-*s", cols - 2, "");

			int bars = syn_iwd_signal_bars(n->signal_strength);
			char glyph[16];
			signal_glyph(bars, glyph);

			mvprintw(2 + row, 2, "%s %-32s", n->connected ? "●" : " ", n->name);
			attroff(COLOR_PAIR(color));

			attron(COLOR_PAIR(is_sel ? color : P_DIM));
			mvprintw(2 + row, 38, "%-7s", security_label(n->type));
			mvprintw(2 + row, 47, "%s", glyph);
			attroff(COLOR_PAIR(is_sel ? color : P_DIM));
		}

		char info[64];
		snprintf(info, sizeof(info), "%s%d/%d", scanning ? "scanning… " : "", count ? selected + 1 : 0, count);
		draw_statusbar(rows, cols, "↑/↓ move   Enter connect   d disconnect   r rescan   Esc/q quit", info);
		refresh();

		int ch = getch();
		switch (ch) {
		case KEY_UP: case 'k': if (count) selected = (selected - 1 + count) % count; break;
		case KEY_DOWN: case 'j': if (count) selected = (selected + 1) % count; break;
		case '\n': case KEY_ENTER: return count ? selected : -1;
		case 'd': return -3;
		case 'r': return -2;
		case 27: case 'q': return -1;
		default: break;
		}
	}
}

int syn_wifi_tui_password_prompt(const char *ssid, char *out, size_t out_len) {
	char buf[256] = {0};
	size_t len = 0;
	char title[300];
	snprintf(title, sizeof(title), "Password for %s", ssid);

	curs_set(1);
	while (1) {
		erase();
		int rows = getmaxy(stdscr), cols = getmaxx(stdscr);
		draw_frame(title);

		attron(COLOR_PAIR(P_NORMAL));
		mvprintw(rows / 2, 2, "> ");
		for (size_t i = 0; i < len; i++) addch('*');
		attroff(COLOR_PAIR(P_NORMAL));
		draw_statusbar(rows, cols, "Enter confirm   Esc cancel   Backspace delete", NULL);
		move(rows / 2, 4 + (int)len);
		refresh();

		int ch = getch();
		if (ch == '\n' || ch == KEY_ENTER) {
			curs_set(0);
			snprintf(out, out_len, "%s", buf);
			memset(buf, 0, sizeof(buf));
			return 0;
		} else if (ch == 27) {
			curs_set(0);
			memset(buf, 0, sizeof(buf));
			return -1;
		} else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (len > 0) buf[--len] = '\0';
		} else if (ch >= 32 && ch < 127 && len < sizeof(buf) - 1) {
			buf[len++] = (char)ch;
			buf[len] = '\0';
		}
	}
}

void syn_wifi_tui_message(const char *title, const char *body) {
	erase();
	int rows = getmaxy(stdscr), cols = getmaxx(stdscr);
	draw_frame(title);
	attron(COLOR_PAIR(P_NORMAL));
	mvprintw(rows / 2, (cols - (int)strlen(body)) / 2, "%s", body);
	attroff(COLOR_PAIR(P_NORMAL));
	draw_statusbar(rows, cols, "Press any key to continue", NULL);
	refresh();
	getch();
}

/* Same layout as syn_wifi_tui_message() but draws and returns immediately
 * — for a status screen shown right before a real blocking call (e.g. the
 * Wi-Fi scan), where waiting on a keypress here would just be one more
 * thing blocking before the actual wait even starts. */
void syn_wifi_tui_message_noinput(const char *title, const char *body) {
	erase();
	int rows = getmaxy(stdscr), cols = getmaxx(stdscr);
	draw_frame(title);
	attron(COLOR_PAIR(P_NORMAL));
	mvprintw(rows / 2, (cols - (int)strlen(body)) / 2, "%s", body);
	attroff(COLOR_PAIR(P_NORMAL));
	draw_statusbar(rows, cols, "Please wait…", NULL);
	refresh();
}

/* Redraws syn_wifi_tui_message_noinput()'s screen with a spinner glyph
 * appended, advancing one frame each call — meant to be called repeatedly
 * (e.g. once per scan poll tick) so a wait that takes real seconds shows
 * visible motion instead of sitting static. */
void syn_wifi_tui_message_spin(const char *title, const char *body) {
	static const char *frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
	static int frame = 0;

	erase();
	int rows = getmaxy(stdscr), cols = getmaxx(stdscr);
	draw_frame(title);
	char line[256];
	snprintf(line, sizeof(line), "%s  %s", body, frames[frame]);
	frame = (frame + 1) % (int)(sizeof(frames) / sizeof(frames[0]));
	attron(COLOR_PAIR(P_NORMAL));
	mvprintw(rows / 2, (cols - (int)strlen(line)) / 2, "%s", line);
	attroff(COLOR_PAIR(P_NORMAL));
	draw_statusbar(rows, cols, "Please wait…", NULL);
	refresh();
}
