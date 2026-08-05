/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_tui_dirpicker.h"
#include "syn_theme.h"

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <libgen.h>

#define COLOR_PAIR_NORMAL SYN_THEME_PAIR_NORMAL
#define COLOR_PAIR_SELECTED SYN_THEME_PAIR_SELECTED
#define COLOR_PAIR_TITLE SYN_THEME_PAIR_TITLE
#define COLOR_PAIR_BORDER SYN_THEME_PAIR_BORDER
#define COLOR_PAIR_DIM SYN_THEME_PAIR_DIM
#define COLOR_PAIR_STATUSBAR SYN_THEME_PAIR_STATUSBAR
#define COLOR_PAIR_DIR SYN_THEME_PAIR_COUNT

static void draw_frame(const char *title) {
	int cols = getmaxx(stdscr);
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

/* Directories only — this picker is for finding a directory (a profile
 * dir, or any other target directory a caller wants), not browsing
 * files, so files never need to appear in the listing at all (unlike
 * syn-crypter's own file picker, which this was ported from and shows
 * both). */
typedef struct {
	char name[256];
	bool has_marker; /* this dir directly contains `marker_filename` —
	                   * shown as a hint so the right one stands out
	                   * among siblings. Meaningless (always false) when
	                   * the caller passed no marker_filename. */
} dir_entry;

static int dir_entry_cmp(const void *a, const void *b) {
	const dir_entry *da = a, *db = b;
	return strcmp(da->name, db->name);
}

static int list_subdirs(const char *path, const char *marker_filename, dir_entry **out_entries) {
	DIR *d = opendir(path);
	if (!d) {
		return -1;
	}

	size_t cap = 64, count = 0;
	dir_entry *entries = malloc(cap * sizeof(dir_entry));
	if (!entries) {
		closedir(d);
		return -1;
	}

	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		if (de->d_name[0] == '.') {
			continue; /* hidden dirs and . / .. all skipped — nothing
			           * useful for finding a profile lives under a
			           * dotdir in practice, and it keeps the listing
			           * free of noise. */
		}
		char full[4096];
		snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
		struct stat st;
		if (stat(full, &st) != 0 || !S_ISDIR(st.st_mode)) {
			continue;
		}

		if (count == cap) {
			cap *= 2;
			dir_entry *grown = realloc(entries, cap * sizeof(dir_entry));
			if (!grown) {
				break;
			}
			entries = grown;
		}

		strncpy(entries[count].name, de->d_name, sizeof(entries[count].name) - 1);
		entries[count].name[sizeof(entries[count].name) - 1] = '\0';

		if (marker_filename) {
			char probe[4160];
			snprintf(probe, sizeof(probe), "%s/%s", full, marker_filename);
			entries[count].has_marker = (access(probe, F_OK) == 0);
		} else {
			entries[count].has_marker = false;
		}
		count++;
	}
	closedir(d);

	qsort(entries, count, sizeof(dir_entry), dir_entry_cmp);
	*out_entries = entries;
	return (int)count;
}

static void draw_column(int y, int x, int height, int width,
		const dir_entry *entries, int count, int sel, int scroll_top, int dim) {
	for (int row = 0; row < height && scroll_top + row < count; row++) {
		int i = scroll_top + row;
		int is_sel = (i == sel);
		int color = is_sel ? COLOR_PAIR_SELECTED : (dim ? COLOR_PAIR_DIM : COLOR_PAIR_DIR);

		attron(COLOR_PAIR(color));
		mvprintw(y + row, x, "%-*s", width, "");

		char label[300];
		snprintf(label, sizeof(label), "%s/%s", entries[i].name, entries[i].has_marker ? " *" : "");
		if ((int)strlen(label) > width - 1) {
			label[width - 1 >= 0 ? width - 1 : 0] = '\0';
		}
		mvprintw(y + row, x, "%s", label);
		attroff(COLOR_PAIR(color));
	}
}

int syn_tui_dirpicker(const char *title, const char *start_dir, const char *marker_filename, char *out, size_t out_len) {
	char cwd[4096];
	strncpy(cwd, start_dir, sizeof(cwd) - 1);
	cwd[sizeof(cwd) - 1] = '\0';

	/* start_dir may not exist yet (e.g. an output directory nothing has
	 * written to yet) — walk up to the nearest real ancestor instead of
	 * opendir() failing on the very first frame and silently cancelling
	 * with zero feedback, which is exactly what a nonexistent
	 * ~/.local/share/syn-os/iso-builder/isos/ did before this fix. */
	{
		struct stat st;
		while (stat(cwd, &st) != 0 || !S_ISDIR(st.st_mode)) {
			char *slash = strrchr(cwd, '/');
			if (!slash || slash == cwd) {
				strncpy(cwd, "/", sizeof(cwd) - 1);
				cwd[sizeof(cwd) - 1] = '\0';
				break;
			}
			*slash = '\0';
			if (cwd[0] == '\0') {
				strncpy(cwd, "/", sizeof(cwd) - 1);
				cwd[sizeof(cwd) - 1] = '\0';
			}
		}
	}

	int selected = 0;
	int scroll_top = 0;

	while (1) {
		dir_entry *entries;
		int count = list_subdirs(cwd, marker_filename, &entries);
		if (count < 0) {
			return -1;
		}
		if (selected >= count) {
			selected = count > 0 ? count - 1 : 0;
		}

		char cwd_copy_for_parent[4096];
		strncpy(cwd_copy_for_parent, cwd, sizeof(cwd_copy_for_parent) - 1);
		cwd_copy_for_parent[sizeof(cwd_copy_for_parent) - 1] = '\0';
		char *parent_path = dirname(cwd_copy_for_parent);
		dir_entry *parent_entries = NULL;
		int parent_count = 0;
		int parent_sel = -1;
		if (strcmp(parent_path, cwd) != 0) {
			parent_count = list_subdirs(parent_path, marker_filename, &parent_entries);
			if (parent_count > 0) {
				char cwd_basename_copy[4096];
				strncpy(cwd_basename_copy, cwd, sizeof(cwd_basename_copy) - 1);
				cwd_basename_copy[sizeof(cwd_basename_copy) - 1] = '\0';
				char *base = basename(cwd_basename_copy);
				for (int i = 0; i < parent_count; i++) {
					if (strcmp(parent_entries[i].name, base) == 0) {
						parent_sel = i;
						break;
					}
				}
			}
		}

		bool cwd_has_marker = false;
		if (marker_filename) {
			char here_probe[4160];
			snprintf(here_probe, sizeof(here_probe), "%s/%s", cwd, marker_filename);
			cwd_has_marker = (access(here_probe, F_OK) == 0);
		}

		int rows, cols;
		while (1) {
			erase();
			getmaxyx(stdscr, rows, cols);
			draw_frame(title);

			attron(COLOR_PAIR(COLOR_PAIR_NORMAL) | A_BOLD);
			mvprintw(1, 2, "%-*.*s", cols - 4, cols - 4, cwd);
			attroff(COLOR_PAIR(COLOR_PAIR_NORMAL) | A_BOLD);
			if (cwd_has_marker) {
				attron(COLOR_PAIR(COLOR_PAIR_DIR) | A_BOLD);
				mvprintw(2, 2, "%s found here — press 's' to select this directory", marker_filename);
				attroff(COLOR_PAIR(COLOR_PAIR_DIR) | A_BOLD);
			}

			int content_top = 4, content_height = rows - 6;
			int parent_w = cols / 5;
			int preview_w = cols / 4;
			int current_x = parent_w + 2;
			int current_w = cols - parent_w - preview_w - 6;
			int preview_x = current_x + current_w + 2;

			if (parent_count > 0) {
				draw_column(content_top, 2, content_height, parent_w,
					parent_entries, parent_count, parent_sel, 0, 1);
			}

			int list_height = content_height;
			if (selected < scroll_top) {
				scroll_top = selected;
			}
			if (selected >= scroll_top + list_height) {
				scroll_top = selected - list_height + 1;
			}
			draw_column(content_top, current_x, content_height, current_w,
				entries, count, selected, scroll_top, 0);

			attron(COLOR_PAIR(COLOR_PAIR_DIM));
			for (int row = 0; row < content_height; row++) {
				mvprintw(content_top + row, preview_x, "%-*s", preview_w, "");
			}
			attroff(COLOR_PAIR(COLOR_PAIR_DIM));
			if (count > 0) {
				char child_path[4096];
				snprintf(child_path, sizeof(child_path), "%s/%s", cwd, entries[selected].name);
				dir_entry *child_entries;
				int child_count = list_subdirs(child_path, marker_filename, &child_entries);
				if (child_count >= 0) {
					draw_column(content_top, preview_x, content_height, preview_w,
						child_entries, child_count, -1, 0, 1);
					free(child_entries);
				}
			}

			attron(COLOR_PAIR(COLOR_PAIR_BORDER));
			for (int row = content_top; row < content_top + content_height; row++) {
				mvaddch(row, current_x - 1, ACS_VLINE);
				mvaddch(row, preview_x - 1, ACS_VLINE);
			}
			attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

			char info[64] = "";
			if (count > 0) {
				snprintf(info, sizeof(info), "%d/%d", selected + 1, count);
			}
			draw_statusbar(rows, cols, "j/k move   l/Enter open   h/Bksp up-dir   s select this dir   Esc/q cancel", info);
			refresh();

			int ch = getch();
			switch (ch) {
			case KEY_UP:
			case 'k':
				if (count > 0) selected = (selected - 1 + count) % count;
				break;
			case KEY_DOWN:
			case 'j':
				if (count > 0) selected = (selected + 1) % count;
				break;
			case KEY_BACKSPACE:
			case 127:
			case 8:
			case KEY_LEFT:
			case 'h':
				free(entries);
				free(parent_entries);
				{
					char *cwd_copy = strdup(cwd);
					char *parent = dirname(cwd_copy);
					if (strcmp(parent, cwd) != 0) {
						strncpy(cwd, parent, sizeof(cwd) - 1);
						cwd[sizeof(cwd) - 1] = '\0';
						selected = 0;
						scroll_top = 0;
					}
					free(cwd_copy);
				}
				goto reload_dir;
			case 's':
			case 'S':
				snprintf(out, out_len, "%s", cwd);
				free(entries);
				free(parent_entries);
				return 0;
			case 27:
			case 'q':
				free(entries);
				free(parent_entries);
				return -1;
			case '\n':
			case KEY_ENTER:
			case KEY_RIGHT:
			case 'l':
				if (count == 0) {
					break;
				}
				{
					char new_cwd[4096];
					snprintf(new_cwd, sizeof(new_cwd), "%s/%s", cwd, entries[selected].name);
					strncpy(cwd, new_cwd, sizeof(cwd) - 1);
					cwd[sizeof(cwd) - 1] = '\0';
					selected = 0;
					scroll_top = 0;
					free(entries);
					free(parent_entries);
					goto reload_dir;
				}
			default:
				break;
			}
		}
	reload_dir:
		continue;
	}
}
