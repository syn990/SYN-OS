/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_tui_modal.h"
#include "syn_theme.h"

#include <ncurses.h>
#include <string.h>

#define COLOR_PAIR_NORMAL SYN_THEME_PAIR_NORMAL
#define COLOR_PAIR_TITLE SYN_THEME_PAIR_TITLE
#define COLOR_PAIR_BORDER SYN_THEME_PAIR_BORDER
#define COLOR_PAIR_DIM SYN_THEME_PAIR_DIM

#define MODAL_WIDTH 44
#define MODAL_HEIGHT 6

int syn_tui_modal_password_prompt(const char *title, char *out, size_t out_len) {
	int screen_rows, screen_cols;
	getmaxyx(stdscr, screen_rows, screen_cols);

	int width = MODAL_WIDTH < screen_cols - 2 ? MODAL_WIDTH : screen_cols - 2;
	int height = MODAL_HEIGHT;
	int start_y = (screen_rows - height) / 2;
	int start_x = (screen_cols - width) / 2;

	WINDOW *win = newwin(height, width, start_y, start_x);
	if (!win) {
		return -1;
	}

	char buf[512] = {0};
	size_t len = 0;
	int result = -1;

	curs_set(1);
	while (1) {
		werase(win);
		wattron(win, COLOR_PAIR(COLOR_PAIR_BORDER));
		box(win, 0, 0);
		wattroff(win, COLOR_PAIR(COLOR_PAIR_BORDER));

		wattron(win, COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);
		mvwprintw(win, 0, (width - (int)strlen(title) - 2) / 2, " %s ", title);
		wattroff(win, COLOR_PAIR(COLOR_PAIR_TITLE) | A_BOLD);

		wattron(win, COLOR_PAIR(COLOR_PAIR_NORMAL));
		mvwprintw(win, 2, 2, "> ");
		for (size_t i = 0; i < len; i++) {
			waddch(win, '*');
		}
		wattroff(win, COLOR_PAIR(COLOR_PAIR_NORMAL));

		wattron(win, COLOR_PAIR(COLOR_PAIR_DIM));
		mvwprintw(win, height - 2, 2, "Enter confirm  Esc cancel");
		wattroff(win, COLOR_PAIR(COLOR_PAIR_DIM));

		wmove(win, 2, 4 + (int)len);
		wrefresh(win);

		int ch = wgetch(win);
		if (ch == '\n' || ch == KEY_ENTER) {
			if (out_len > 0) {
				strncpy(out, buf, out_len - 1);
				out[out_len - 1] = '\0';
			}
			result = 0;
			break;
		} else if (ch == 27) {
			result = -1;
			break;
		} else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (len > 0) {
				buf[--len] = '\0';
			}
		} else if (ch >= 32 && ch < 127 && len < sizeof(buf) - 1) {
			buf[len++] = (char)ch;
			buf[len] = '\0';
		}
	}

	memset(buf, 0, sizeof(buf));
	curs_set(0);
	delwin(win);
	touchwin(stdscr);
	refresh();
	return result;
}
