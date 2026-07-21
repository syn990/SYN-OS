/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-AUDIO (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_audio_tui.h"
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
#define P_ACCENT SYN_THEME_PAIR_COUNT /* slot 18 (theme accent), same convention as syn-wifi */

void syn_audio_tui_init(void) {
	/* Same C.utf8 fallback as syn-wifi: launched via `foot -e syn-audio`
	 * with no doas involved here, but LANG can still be unset/mismatched
	 * in some launch contexts (e.g. a bare waybar on-click exec), so this
	 * guards the box-drawing glyphs below the same way regardless. */
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
		init_pair(P_ACCENT, 18, -1);
	}
	refresh();
}

void syn_audio_tui_end(void) { endwin(); }

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

/* 10-cell volume bar, filled proportionally to pct (0-100, clamped). */
static void volume_glyph(int pct, char *out /* needs 31 bytes: 10 x 3-byte UTF-8 cell + NUL */) {
	if (pct < 0) pct = 0;
	if (pct > 100) pct = 100;
	int filled = (pct * 10 + 50) / 100;
	out[0] = '\0';
	for (int i = 0; i < 10; i++) {
		strcat(out, i < filled ? "█" : "░");
	}
}

static void draw_device_list(const syn_pulse_device *devices, int count, int selected,
		int scroll_top, int row_start, int list_rows, int cols, bool focused) {
	for (int row = 0; row < list_rows && scroll_top + row < count; row++) {
		int i = scroll_top + row;
		const syn_pulse_device *d = &devices[i];
		bool is_sel = focused && (i == selected);
		int color = is_sel ? P_SELECTED : P_NORMAL;

		attron(COLOR_PAIR(color));
		mvprintw(row_start + row, 1, "%-*s", cols - 2, "");

		char glyph[32];
		volume_glyph(d->volume_pct, glyph);

		mvprintw(row_start + row, 2, "%s%s %-30.30s",
			d->is_default ? "●" : " ", d->muted ? "M" : " ", d->description);
		attroff(COLOR_PAIR(color));

		attron(COLOR_PAIR(is_sel ? color : P_DIM));
		mvprintw(row_start + row, 36, "%s %3d%%", glyph, d->volume_pct);
		attroff(COLOR_PAIR(is_sel ? color : P_DIM));
	}
	if (count == 0) {
		attron(COLOR_PAIR(P_DIM));
		mvprintw(row_start, 2, "(none found)");
		attroff(COLOR_PAIR(P_DIM));
	}
}

syn_audio_input_result syn_audio_tui_dashboard(
		const syn_pulse_device *outputs, int output_count,
		const syn_pulse_device *inputs, int input_count,
		syn_audio_tab focused_tab, int selected_index) {
	erase();
	int rows = getmaxy(stdscr), cols = getmaxx(stdscr);
	draw_frame("SYN-OS Audio");

	int half = (rows - 4) / 2;

	attron(COLOR_PAIR(P_ACCENT) | A_BOLD);
	mvprintw(1, 2, "OUTPUTS");
	attroff(COLOR_PAIR(P_ACCENT) | A_BOLD);
	draw_device_list(outputs, output_count,
		focused_tab == SYN_AUDIO_TAB_OUTPUTS ? selected_index : -1,
		0, 2, half, cols, focused_tab == SYN_AUDIO_TAB_OUTPUTS);

	int inputs_row = 2 + half + 1;
	attron(COLOR_PAIR(P_ACCENT) | A_BOLD);
	mvprintw(inputs_row - 1, 2, "INPUTS");
	attroff(COLOR_PAIR(P_ACCENT) | A_BOLD);
	draw_device_list(inputs, input_count,
		focused_tab == SYN_AUDIO_TAB_INPUTS ? selected_index : -1,
		0, inputs_row, rows - 2 - inputs_row, cols, focused_tab == SYN_AUDIO_TAB_INPUTS);

	draw_statusbar(rows, cols,
		"Tab switch   ↑/↓ move   Enter default   m mute   ←/→ volume   Esc/q quit", NULL);
	refresh();

	syn_audio_input_result result = {0};
	result.tab = focused_tab;
	result.index = selected_index;

	int ch = getch();
	switch (ch) {
	case '\t':
		result.action = SYN_AUDIO_ACTION_SWITCH_TAB;
		break;
	case '\n': case KEY_ENTER:
		result.action = SYN_AUDIO_ACTION_SET_DEFAULT;
		break;
	case 'm': case 'M':
		result.action = SYN_AUDIO_ACTION_TOGGLE_MUTE;
		break;
	case KEY_RIGHT: case 'l':
		result.action = SYN_AUDIO_ACTION_VOLUME_UP;
		break;
	case KEY_LEFT: case 'h':
		result.action = SYN_AUDIO_ACTION_VOLUME_DOWN;
		break;
	case 27: case 'q':
		result.action = SYN_AUDIO_ACTION_QUIT;
		break;
	case KEY_UP: case 'k': {
		int count = focused_tab == SYN_AUDIO_TAB_OUTPUTS ? output_count : input_count;
		if (count > 0) result.index = (selected_index - 1 + count) % count;
		result.action = SYN_AUDIO_ACTION_NONE;
		break;
	}
	case KEY_DOWN: case 'j': {
		int count = focused_tab == SYN_AUDIO_TAB_OUTPUTS ? output_count : input_count;
		if (count > 0) result.index = (selected_index + 1) % count;
		result.action = SYN_AUDIO_ACTION_NONE;
		break;
	}
	default:
		result.action = SYN_AUDIO_ACTION_NONE;
		break;
	}
	return result;
}

void syn_audio_tui_message(const char *title, const char *body) {
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
