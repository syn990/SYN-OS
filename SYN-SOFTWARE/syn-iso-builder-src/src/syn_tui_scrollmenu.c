/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_tui_scrollmenu.h"
#include "syn_theme.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#define COLOR_PAIR_NORMAL SYN_THEME_PAIR_NORMAL
#define COLOR_PAIR_SELECTED SYN_THEME_PAIR_SELECTED
#define COLOR_PAIR_TITLE SYN_THEME_PAIR_TITLE
#define COLOR_PAIR_BORDER SYN_THEME_PAIR_BORDER
#define COLOR_PAIR_DIM SYN_THEME_PAIR_DIM
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

static void draw_row(int y, int x, int width, const syn_scrollmenu_row *row,
	const syn_scrollmenu_layout *layout, int is_selected) {
	if (is_selected) {
		attron(COLOR_PAIR(COLOR_PAIR_SELECTED));
	} else {
		attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
	}

	mvprintw(y, x, "%-*s", width, "");

	int col_x = x;
	int fixed_width_used = 0;
	int flex_col = -1;
	for (int c = 0; c < layout->count; c++) {
		if (layout->widths[c] == 0) {
			flex_col = c;
		} else {
			fixed_width_used += layout->widths[c] + 1;
		}
	}
	int flex_width = width - fixed_width_used;
	if (flex_width < 4) {
		flex_width = 4;
	}

	for (int c = 0; c < layout->count; c++) {
		int w = (c == flex_col) ? flex_width : layout->widths[c];
		mvprintw(y, col_x, "%-*.*s", w, w, row->columns[c]);
		col_x += w + 1;
	}

	if (is_selected) {
		attroff(COLOR_PAIR(COLOR_PAIR_SELECTED));
	} else {
		attroff(COLOR_PAIR(COLOR_PAIR_NORMAL));
	}
}

/* Case-sensitive substring match — plain strstr, no fuzzy-match library
 * (matches this repo's "no unrequested complexity" default). */
static int row_matches_filter(const syn_scrollmenu_row *row, const char *filter) {
	if (!filter || filter[0] == '\0') {
		return 1;
	}
	return strstr(row->filter_text, filter) != NULL;
}

int syn_tui_scrollmenu(const char *title, const syn_scrollmenu_row *rows, int count,
	const syn_scrollmenu_layout *layout) {
	char filter[256] = {0};
	size_t filter_len = 0;
	int selected = 0;
	int scroll_top = 0;

	/* Indices into `rows` that currently pass the filter — rebuilt on
	 * every filter-text change. */
	int *visible = malloc(sizeof(int) * (size_t)(count > 0 ? count : 1));
	int visible_count = 0;
	for (int i = 0; i < count; i++) {
		if (row_matches_filter(&rows[i], filter)) {
			visible[visible_count++] = i;
		}
	}

	int rows_dim, cols_dim;
	int input_mode = 0; /* 1 while editing the filter line */

	while (1) {
		erase();
		getmaxyx(stdscr, rows_dim, cols_dim);
		draw_frame(title);

		int content_top = 2;
		int content_height = rows_dim - content_top - 2; /* leave room for filter line + statusbar */
		if (content_height < 1) {
			content_height = 1;
		}

		/* Filter line, row 1. */
		attron(COLOR_PAIR(COLOR_PAIR_DIM));
		mvprintw(1, 2, "Filter: %s%s", filter, input_mode ? "_" : "");
		attroff(COLOR_PAIR(COLOR_PAIR_DIM));

		if (selected >= visible_count) {
			selected = visible_count > 0 ? visible_count - 1 : 0;
		}
		if (selected < scroll_top) {
			scroll_top = selected;
		}
		if (selected >= scroll_top + content_height) {
			scroll_top = selected - content_height + 1;
		}

		for (int i = 0; i < content_height && scroll_top + i < visible_count; i++) {
			int row_idx = visible[scroll_top + i];
			draw_row(content_top + i, 2, cols_dim - 4, &rows[row_idx], layout, scroll_top + i == selected);
		}

		char info[64];
		snprintf(info, sizeof(info), "%d/%d", visible_count > 0 ? selected + 1 : 0, visible_count);
		draw_statusbar(rows_dim, cols_dim, "Up/Down j/k move   / filter   Enter select   Esc/q cancel", info);
		refresh();

		int ch = getch();

		if (input_mode) {
			if (ch == '\n' || ch == KEY_ENTER || ch == 27) {
				input_mode = 0;
			} else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
				if (filter_len > 0) {
					filter[--filter_len] = '\0';
				}
			} else if (ch >= 32 && ch < 127 && filter_len < sizeof(filter) - 1) {
				filter[filter_len++] = (char)ch;
				filter[filter_len] = '\0';
			} else {
				continue;
			}
			visible_count = 0;
			for (int i = 0; i < count; i++) {
				if (row_matches_filter(&rows[i], filter)) {
					visible[visible_count++] = i;
				}
			}
			selected = 0;
			scroll_top = 0;
			continue;
		}

		switch (ch) {
		case KEY_UP:
		case 'k':
			if (visible_count > 0) selected = (selected - 1 + visible_count) % visible_count;
			break;
		case KEY_DOWN:
		case 'j':
			if (visible_count > 0) selected = (selected + 1) % visible_count;
			break;
		case '/':
			input_mode = 1;
			break;
		case 27:
		case 'q':
			free(visible);
			return -1;
		case '\n':
		case KEY_ENTER:
			if (visible_count == 0) {
				break;
			}
			{
				int result = visible[selected];
				free(visible);
				return result;
			}
		default:
			break;
		}
	}
}
