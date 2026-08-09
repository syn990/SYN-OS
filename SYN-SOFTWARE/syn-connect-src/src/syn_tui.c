/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CONNECT (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_tui.h"
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

static const char *TAB_LABELS[SYN_TUI_TAB_COUNT] = {"Wi-Fi", "Bluetooth", "Ethernet", "VPN"};

/* Selected row remembered per tab across redraws (e.g. after a rescan)
 * so switching tabs and back doesn't reset the cursor to the top. */
static int s_selected[SYN_TUI_TAB_COUNT] = {0};
static int s_scroll_top[SYN_TUI_TAB_COUNT] = {0};

void syn_tui_init(void) {
	/* doas strips LANG/LC_* from the environment before exec'ing the
	 * target program (confirmed: `doas env | grep LANG` returns nothing,
	 * even though the invoking shell has LANG=en_GB.UTF-8) — and this can
	 * run under `foot -e doas /usr/lib/syn-os/syn-connect`, so
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
		init_pair(P_ACCENT, 18, -1); /* theme accent, e.g. security/type label */
	}
	refresh();
}

void syn_tui_end(void) { endwin(); }

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

/* Tab strip drawn as row 1, just under the title border, active tab
 * highlighted, so Tab/Shift+Tab has a visible target. */
static void draw_tabs(syn_tui_tab active) {
	int col = 2;
	for (int t = 0; t < SYN_TUI_TAB_COUNT; t++) {
		bool is_active = (t == (int)active);
		attron(COLOR_PAIR(is_active ? P_SELECTED : P_DIM));
		mvprintw(1, col, " %s ", TAB_LABELS[t]);
		attroff(COLOR_PAIR(is_active ? P_SELECTED : P_DIM));
		col += (int)strlen(TAB_LABELS[t]) + 3;
	}
}

/* 4-cell bar, each cell either filled or empty depending on `bars` (0-4). */
static void signal_glyph(int bars, char *out /* needs 13 bytes: 4 x 3-byte UTF-8 cell + NUL */) {
	out[0] = '\0';
	for (int i = 0; i < 4; i++) {
		strcat(out, i < bars ? "█" : "░");
	}
}

static void draw_row(int row, int cols, const syn_tui_row *r, bool is_sel) {
	int color = is_sel ? P_SELECTED : P_NORMAL;
	attron(COLOR_PAIR(color));
	mvprintw(row, 1, "%-*s", cols - 2, "");
	mvprintw(row, 3, "%s %-32s", r->connected ? "●" : " ", r->label);
	attroff(COLOR_PAIR(color));

	attron(COLOR_PAIR(is_sel ? color : P_DIM));
	if (r->badge[0]) {
		mvprintw(row, 39, "%-7s", r->badge);
	}
	if (r->signal_bars >= 0) {
		char glyph[16];
		signal_glyph(r->signal_bars, glyph);
		mvprintw(row, 48, "%s", glyph);
	}
	attroff(COLOR_PAIR(is_sel ? color : P_DIM));
}

/* One dashboard display row: either a section header ("Connected" /
 * "Available", entry_index < 0) or a real list entry (entry_index is
 * the position within the caller's rows[tab] array — NOT the display
 * position, since the connected section reorders things). Built fresh
 * each frame from whatever the caller currently has, so a rescan/
 * connect/pair result is reflected immediately without this function
 * tracking state across calls beyond the remembered selection/scroll
 * position. */
typedef struct {
	int entry_index;
	bool is_header;
	const char *header_label;
} display_row;

/* Splits `rows[0..count)` into a "Connected" section (entries with
 * `connected` set — the caller sets this from real live-connection
 * state, e.g. BlueZ's own Connected property, not saved pairing/trust,
 * which doesn't change just because a device is or isn't in range right
 * now) above an "Available" section (everything else), each under its
 * own header. Returns the number of display_row entries written to
 * `out` (headers + entries; caller must size out for 2 + count). */
static int build_display_rows(const syn_tui_row *rows, int count, display_row *out) {
	int n = 0;
	bool any_connected = false;
	for (int i = 0; i < count; i++) if (rows[i].connected) any_connected = true;

	if (any_connected) {
		out[n++] = (display_row){.is_header = true, .header_label = "Connected", .entry_index = -1};
		for (int i = 0; i < count; i++) {
			if (rows[i].connected) out[n++] = (display_row){.entry_index = i};
		}
	}

	bool any_available = false;
	for (int i = 0; i < count; i++) if (!rows[i].connected) any_available = true;

	if (any_available) {
		out[n++] = (display_row){.is_header = true, .header_label = "Available", .entry_index = -1};
		for (int i = 0; i < count; i++) {
			if (!rows[i].connected) out[n++] = (display_row){.entry_index = i};
		}
	}
	return n;
}

static const char *no_scan_prompt(syn_tui_tab tab) {
	switch (tab) {
	case SYN_TUI_TAB_WIFI: return "No scan yet — press r to scan for Wi-Fi networks";
	case SYN_TUI_TAB_BLUETOOTH: return "No scan yet — press r to scan for nearby Bluetooth devices";
	case SYN_TUI_TAB_ETHERNET: return "No status yet — press r to check the Ethernet link";
	case SYN_TUI_TAB_VPN: return "No configs loaded yet — press r to look for VPN configs";
	default: return "No scan yet — press r";
	}
}

static const char *empty_prompt(syn_tui_tab tab) {
	switch (tab) {
	case SYN_TUI_TAB_WIFI: return "No networks found — press r to rescan";
	case SYN_TUI_TAB_BLUETOOTH: return "No devices found — press r to scan again";
	case SYN_TUI_TAB_VPN: return "No .ovpn configs found in ~/.ovpn — press r to check again";
	default: return "Nothing found — press r to check again";
	}
}

static const char *tab_hint(syn_tui_tab tab) {
	switch (tab) {
	case SYN_TUI_TAB_WIFI: return "Tab switch   ↑/↓ move   Enter connect   d disconnect   r rescan   Esc/q quit";
	case SYN_TUI_TAB_BLUETOOTH: return "Tab switch   ↑/↓ move   Enter pair+connect   d disconnect   x forget   r rescan   Esc/q quit";
	case SYN_TUI_TAB_ETHERNET: return "Tab switch   r refresh   Esc/q quit";
	case SYN_TUI_TAB_VPN: return "Tab switch   ↑/↓ move   Enter connect   d disconnect   r refresh   Esc/q quit";
	default: return "Tab switch   Esc/q quit";
	}
}

static void draw_eth_status(int top_row, int cols, const syn_tui_eth_status *eth) {
	(void)cols;
	if (!eth || !eth->has_link) {
		attron(COLOR_PAIR(P_DIM));
		mvprintw(top_row + 2, 4, "No Ethernet interface found.");
		attroff(COLOR_PAIR(P_DIM));
		return;
	}

	attron(COLOR_PAIR(P_ACCENT) | A_BOLD);
	mvprintw(top_row, 4, "%s", eth->ifname);
	attroff(COLOR_PAIR(P_ACCENT) | A_BOLD);

	attron(COLOR_PAIR(eth->carrier ? P_NORMAL : P_DIM));
	mvprintw(top_row + 2, 4, "%s  %s", eth->carrier ? "●" : "○",
	         eth->carrier ? "Cable connected" : "No cable connected");
	attroff(COLOR_PAIR(eth->carrier ? P_NORMAL : P_DIM));

	attron(COLOR_PAIR(P_DIM));
	mvprintw(top_row + 4, 4, "Link:    %s", eth->up ? "up" : "down");
	mvprintw(top_row + 5, 4, "Address: %s", eth->ipv4[0] ? eth->ipv4 : "(none)");
	mvprintw(top_row + 6, 4, "MAC:     %s", eth->mac[0] ? eth->mac : "(unknown)");
	attroff(COLOR_PAIR(P_DIM));
}

syn_tui_action syn_tui_picker(syn_tui_tab *tab,
                               const syn_tui_row *rows[SYN_TUI_TAB_COUNT], const int row_counts[SYN_TUI_TAB_COUNT],
                               const bool scanned[SYN_TUI_TAB_COUNT], const syn_tui_eth_status *eth,
                               int scanning, int *index) {
	while (1) {
		int t = (int)*tab;
		bool is_eth = (*tab == SYN_TUI_TAB_ETHERNET);
		int count = is_eth ? 0 : row_counts[t];
		bool tab_scanned = scanned[t];
		int *selected = &s_selected[t];

		display_row rows_buf[2 + 64];
		int row_count = is_eth ? 0 : build_display_rows(rows[t], count, rows_buf);

		if (*selected >= row_count) *selected = row_count > 0 ? row_count - 1 : 0;
		if (*selected < 0) *selected = 0;
		/* Selection must land on a real entry, never a header — headers
		 * exist purely as separators between the two sections. */
		while (*selected < row_count && rows_buf[*selected].is_header) (*selected)++;
		if (*selected >= row_count) *selected = 0;

		erase();
		int scr_rows = getmaxy(stdscr), cols = getmaxx(stdscr);
		draw_frame("SYN-Connect");
		draw_tabs(*tab);

		int *scroll_top = &s_scroll_top[t];
		int list_rows = scr_rows - 4;
		if (*selected < *scroll_top) *scroll_top = *selected;
		if (*selected >= *scroll_top + list_rows) *scroll_top = *selected - list_rows + 1;

		int entry_count = 0, selected_pos = 0;

		if (is_eth) {
			if (!tab_scanned) {
				attron(COLOR_PAIR(P_DIM));
				mvprintw(3 + list_rows / 2, (cols - (int)strlen(no_scan_prompt(*tab))) / 2, "%s", no_scan_prompt(*tab));
				attroff(COLOR_PAIR(P_DIM));
			} else {
				draw_eth_status(3, cols, eth);
			}
		} else if (!tab_scanned) {
			attron(COLOR_PAIR(P_DIM));
			mvprintw(3 + list_rows / 2, (cols - (int)strlen(no_scan_prompt(*tab))) / 2, "%s", no_scan_prompt(*tab));
			attroff(COLOR_PAIR(P_DIM));
		} else if (row_count == 0 && !scanning) {
			attron(COLOR_PAIR(P_DIM));
			mvprintw(3 + list_rows / 2, (cols - (int)strlen(empty_prompt(*tab))) / 2, "%s", empty_prompt(*tab));
			attroff(COLOR_PAIR(P_DIM));
		} else {
			for (int row = 0; row < list_rows && *scroll_top + row < row_count; row++) {
				int i = *scroll_top + row;
				const display_row *dr = &rows_buf[i];
				if (dr->is_header) {
					attron(COLOR_PAIR(P_ACCENT) | A_BOLD);
					mvprintw(3 + row, 2, "%s", dr->header_label);
					attroff(COLOR_PAIR(P_ACCENT) | A_BOLD);
					continue;
				}
				bool is_sel = (i == *selected);
				draw_row(3 + row, cols, &rows[t][dr->entry_index], is_sel);
			}

			for (int i = 0; i < row_count; i++) {
				if (!rows_buf[i].is_header) {
					if (i == *selected) selected_pos = entry_count;
					entry_count++;
				}
			}
		}

		char info[64];
		if (is_eth) {
			info[0] = '\0';
		} else {
			snprintf(info, sizeof(info), "%s%d/%d", scanning ? "scanning… " : "", entry_count ? selected_pos + 1 : 0, entry_count);
		}
		draw_statusbar(scr_rows, cols, tab_hint(*tab), info);
		refresh();

		int ch = getch();
		switch (ch) {
		case KEY_UP: case 'k':
			if (!is_eth && entry_count) {
				do { *selected = (*selected - 1 + row_count) % row_count; } while (rows_buf[*selected].is_header);
			}
			break;
		case KEY_DOWN: case 'j':
			if (!is_eth && entry_count) {
				do { *selected = (*selected + 1) % row_count; } while (rows_buf[*selected].is_header);
			}
			break;
		case '\t': case KEY_BTAB:
			*tab = (syn_tui_tab)(((int)*tab + 1) % SYN_TUI_TAB_COUNT);
			continue;
		case '\n': case KEY_ENTER:
			if (is_eth || !entry_count) continue;
			*index = rows_buf[*selected].entry_index;
			return SYN_TUI_ACTION_SELECT;
		case 'd':
			if (is_eth) continue;
			*index = entry_count ? rows_buf[*selected].entry_index : -1;
			return SYN_TUI_ACTION_DISCONNECT;
		case 'x':
			if (*tab != SYN_TUI_TAB_BLUETOOTH || !entry_count) continue;
			*index = rows_buf[*selected].entry_index;
			return SYN_TUI_ACTION_REMOVE;
		case 'r':
			return SYN_TUI_ACTION_RESCAN;
		case 27: case 'q':
			return SYN_TUI_ACTION_NONE;
		default: break;
		}
	}
}

static int text_prompt(const char *title, const char *field_label, int field_row, bool masked, char *out, size_t out_len) {
	char buf[256] = {0};
	size_t len = 0;

	curs_set(1);
	while (1) {
		erase();
		int rows = getmaxy(stdscr), cols = getmaxx(stdscr);
		draw_frame(title);

		attron(COLOR_PAIR(P_NORMAL));
		mvprintw(field_row, 2, "%s", field_label);
		mvprintw(field_row, 2 + (int)strlen(field_label), "> ");
		for (size_t i = 0; i < len; i++) addch(masked ? '*' : buf[i]);
		attroff(COLOR_PAIR(P_NORMAL));
		draw_statusbar(rows, cols, "Enter confirm   Esc cancel   Backspace delete", NULL);
		move(field_row, 2 + (int)strlen(field_label) + 2 + (int)len);
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

int syn_tui_password_prompt(const char *title, char *out, size_t out_len) {
	return text_prompt(title, "", getmaxy(stdscr) / 2, true, out, out_len);
}

void syn_tui_message(const char *title, const char *body) {
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

/* Same layout as syn_tui_message() but draws and returns immediately —
 * for a status screen shown right before a real blocking call (e.g. a
 * Wi-Fi/Bluetooth scan), where waiting on a keypress here would just be
 * one more thing blocking before the actual wait even starts. */
void syn_tui_message_noinput(const char *title, const char *body) {
	erase();
	int rows = getmaxy(stdscr), cols = getmaxx(stdscr);
	draw_frame(title);
	attron(COLOR_PAIR(P_NORMAL));
	mvprintw(rows / 2, (cols - (int)strlen(body)) / 2, "%s", body);
	attroff(COLOR_PAIR(P_NORMAL));
	draw_statusbar(rows, cols, "Please wait…", NULL);
	refresh();
}

/* Redraws syn_tui_message_noinput()'s screen with a spinner glyph
 * appended, advancing one frame each call — meant to be called repeatedly
 * (e.g. once per scan poll tick) so a wait that takes real seconds shows
 * visible motion instead of sitting static. */
void syn_tui_message_spin(const char *title, const char *body) {
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
