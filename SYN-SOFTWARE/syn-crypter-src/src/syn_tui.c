/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CRYPTER (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_tui.h"
#include "syn_theme.h"

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>
#include <locale.h>

#define COLOR_PAIR_NORMAL 1
#define COLOR_PAIR_SELECTED 2
#define COLOR_PAIR_TITLE 3
#define COLOR_PAIR_BORDER 4
#define COLOR_PAIR_DIM 5       /* muted entries: parent-dir column, non-file rows */
#define COLOR_PAIR_STATUSBAR 6 /* inverted footer bar, ranger/vim-style */
#define COLOR_PAIR_DIR 7       /* directory names in the current listing */

/* ncurses' init_color takes 0-1000, not 0-255. */
static short scale(short v) {
	return (short)((int)v * 1000 / 255);
}

/* Relative luminance (WCAG), used only to compare two colors' contrast
 * against a third — not for anything display-accurate. */
static double relative_luminance(syn_rgb c) {
	return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b;
}

static double contrast_ratio(syn_rgb a, syn_rgb b) {
	double la = relative_luminance(a), lb = relative_luminance(b);
	if (la < lb) {
		double tmp = la; la = lb; lb = tmp;
	}
	return (la + 0.05 * 255) / (lb + 0.05 * 255);
}

static void apply_theme_colors(void) {
	syn_palette pal;
	syn_theme_load(&pal);

	start_color();

	/* Custom color slots 16-22 (0-15 are the standard ANSI palette every
	 * terminal expects to still mean what they normally mean) — redefined
	 * to the theme's actual RGB via init_color, which only takes effect
	 * on terminals that support palette redefinition (confirmed for foot,
	 * this tool's only real host, via terminfo's initc capability). */
	init_color(16, scale(pal.bg.r), scale(pal.bg.g), scale(pal.bg.b));
	init_color(17, scale(pal.text.r), scale(pal.text.g), scale(pal.text.b));
	init_color(18, scale(pal.accent.r), scale(pal.accent.g), scale(pal.accent.b));
	init_color(19, scale(pal.panel.r), scale(pal.panel.g), scale(pal.panel.b));
	init_color(20, scale(pal.border.r), scale(pal.border.g), scale(pal.border.b));
	init_color(21, scale(pal.accent_dim.r), scale(pal.accent_dim.g), scale(pal.accent_dim.b));

	/* Some themes put text and accent close in luminance (the "glow"
	 * look), which leaves bg-on-accent low-contrast for those themes'
	 * status bar. Pick whichever of bg/text actually contrasts more
	 * against accent, per theme, instead of assuming bg always wins. */
	syn_rgb statusbar_fg = contrast_ratio(pal.bg, pal.accent) >= contrast_ratio(pal.text, pal.accent)
		? pal.bg : pal.text;
	init_color(22, scale(statusbar_fg.r), scale(statusbar_fg.g), scale(statusbar_fg.b));

	init_pair(COLOR_PAIR_NORMAL, 17, 16);
	init_pair(COLOR_PAIR_SELECTED, 16, 18);  /* inverted: bg-colored text on accent background */
	init_pair(COLOR_PAIR_TITLE, 18, 16);
	init_pair(COLOR_PAIR_BORDER, 19, 16);
	init_pair(COLOR_PAIR_DIM, 20, 16);
	init_pair(COLOR_PAIR_STATUSBAR, 22, 18);
	init_pair(COLOR_PAIR_DIR, 18, 16);        /* accent-colored directory names */

	bkgd(COLOR_PAIR(COLOR_PAIR_NORMAL));
}

void syn_tui_init(void) {
	/* Required before initscr() for ncursesw to render multibyte UTF-8
	 * glyphs (▸, ↑, ↓, box-drawing, ...) correctly — without this, the C
	 * library stays in the "C" locale regardless of the environment's
	 * real LANG/LC_* settings, so ncurses receives each UTF-8 character's
	 * individual bytes as separate invalid single-byte characters and
	 * substitutes a fallback glyph per byte instead of the real one. */
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

/* Inverted footer bar on the frame's bottom border row: left-aligned
 * hint, right-aligned info (item counts, "12.3 KB", ...). */
static void draw_statusbar(int rows, int cols, const char *hint, const char *info) {
	attron(COLOR_PAIR(COLOR_PAIR_STATUSBAR));
	mvprintw(rows - 1, 1, "%-*s", cols - 2, "");
	mvprintw(rows - 1, 2, "%s", hint);
	if (info && info[0]) {
		mvprintw(rows - 1, cols - 2 - (int)strlen(info), "%s", info);
	}
	attroff(COLOR_PAIR(COLOR_PAIR_STATUSBAR));
}

int syn_tui_menu(const char *title, const char *const *items, int count) {
	int selected = 0;
	int rows, cols;

	while (1) {
		erase();
		getmaxyx(stdscr, rows, cols);
		draw_frame(title);

		int start_row = 2;
		for (int i = 0; i < count && start_row + i < rows - 2; i++) {
			if (i == selected) {
				attron(COLOR_PAIR(COLOR_PAIR_SELECTED));
				mvprintw(start_row + i, 2, "%-*s", cols - 4, "");
				mvprintw(start_row + i, 2, "▸ %s", items[i]);
			} else {
				attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
				mvprintw(start_row + i, 2, "%-*s", cols - 4, "");
				mvprintw(start_row + i, 4, "%s", items[i]);
			}
			attroff(COLOR_PAIR(COLOR_PAIR_SELECTED) | COLOR_PAIR(COLOR_PAIR_NORMAL));
		}

		char info[32];
		snprintf(info, sizeof(info), "%d/%d", selected + 1, count);
		draw_statusbar(rows, cols, "↑/↓ j/k move   Enter select   Esc/q cancel", info);
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
		case '\n':
		case KEY_ENTER:
			return selected;
		case 27: /* Esc */
		case 'q':
			return -1;
		default:
			break;
		}
	}
}

typedef struct {
	char name[512];
	int is_dir;
	off_t size;
	mode_t mode;
} dir_entry;

static int dir_entry_cmp(const void *a, const void *b) {
	const dir_entry *da = a, *db = b;
	if (da->is_dir != db->is_dir) {
		return db->is_dir - da->is_dir; /* directories first */
	}
	return strcmp(da->name, db->name);
}

static int list_directory(const char *path, dir_entry **out_entries) {
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
		/* ".." is handled by Backspace (see syn_tui_file_picker), not as a
		 * listed row — sorting put it first among directories every time
		 * (".." < any normal name), so an unfiltered listing's index 0 was
		 * ALWAYS ".." rather than the first real entry, silently off-by-
		 * one-ing anything that assumed otherwise. */
		if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
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
		char full[4096];
		snprintf(full, sizeof(full), "%s/%s", path, de->d_name);
		struct stat st;
		int have_stat = (stat(full, &st) == 0);

		strncpy(entries[count].name, de->d_name, sizeof(entries[count].name) - 1);
		entries[count].name[sizeof(entries[count].name) - 1] = '\0';
		entries[count].is_dir = have_stat && S_ISDIR(st.st_mode);
		entries[count].size = have_stat ? st.st_size : 0;
		entries[count].mode = have_stat ? st.st_mode : 0;
		count++;
	}
	closedir(d);

	qsort(entries, count, sizeof(dir_entry), dir_entry_cmp);
	*out_entries = entries;
	return (int)count;
}

static void human_size(off_t bytes, char *out, size_t outlen) {
	static const char *units[] = {"B", "K", "M", "G", "T"};
	double size = (double)bytes;
	size_t unit = 0;
	while (size >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
		size /= 1024.0;
		unit++;
	}
	if (unit == 0) {
		snprintf(out, outlen, "%lld%s", (long long)bytes, units[unit]);
	} else {
		snprintf(out, outlen, "%.1f%s", size, units[unit]);
	}
}

static void mode_string(mode_t mode, int is_dir, char *out /* 10 bytes */) {
	out[0] = is_dir ? 'd' : '-';
	out[1] = (mode & S_IRUSR) ? 'r' : '-';
	out[2] = (mode & S_IWUSR) ? 'w' : '-';
	out[3] = (mode & S_IXUSR) ? 'x' : '-';
	out[4] = (mode & S_IRGRP) ? 'r' : '-';
	out[5] = (mode & S_IWGRP) ? 'w' : '-';
	out[6] = (mode & S_IXGRP) ? 'x' : '-';
	out[7] = (mode & S_IROTH) ? 'r' : '-';
	out[8] = (mode & S_IWOTH) ? 'w' : '-';
	out[9] = '\0';
}

/* Renders one column of the Miller layout. `dim` mutes the whole column
 * (parent-directory/preview columns); `sel` is the highlighted row, -1
 * if this column has no cursor. */
static void draw_column(int y, int x, int height, int width,
		const dir_entry *entries, int count, int sel, int scroll_top, int dim) {
	for (int row = 0; row < height && scroll_top + row < count; row++) {
		int i = scroll_top + row;
		int is_sel = (i == sel);
		int color = is_sel ? COLOR_PAIR_SELECTED : (dim ? COLOR_PAIR_DIM : (entries[i].is_dir ? COLOR_PAIR_DIR : COLOR_PAIR_NORMAL));

		attron(COLOR_PAIR(color));
		mvprintw(y + row, x, "%-*s", width, "");

		char label[256];
		snprintf(label, sizeof(label), "%s%s", entries[i].name, entries[i].is_dir ? "/" : "");
		if ((int)strlen(label) > width - 1) {
			label[width - 1 >= 0 ? width - 1 : 0] = '\0';
		}
		mvprintw(y + row, x, "%s", label);
		attroff(COLOR_PAIR(color));
	}
}

int syn_tui_file_picker(const char *title, const char *start_dir, char *out, size_t out_len) {
	char cwd[4096];
	strncpy(cwd, start_dir, sizeof(cwd) - 1);
	cwd[sizeof(cwd) - 1] = '\0';

	int selected = 0;
	int scroll_top = 0;

	while (1) {
		dir_entry *entries;
		int count = list_directory(cwd, &entries);
		if (count < 0) {
			return -1;
		}
		if (selected >= count) {
			selected = count > 0 ? count - 1 : 0;
		}

		/* Parent column: listing one level up, with the current
		 * directory's basename highlighted. */
		char cwd_copy_for_parent[4096];
		strncpy(cwd_copy_for_parent, cwd, sizeof(cwd_copy_for_parent) - 1);
		cwd_copy_for_parent[sizeof(cwd_copy_for_parent) - 1] = '\0';
		char *parent_path = dirname(cwd_copy_for_parent);
		dir_entry *parent_entries = NULL;
		int parent_count = 0;
		int parent_sel = -1;
		if (strcmp(parent_path, cwd) != 0) {
			parent_count = list_directory(parent_path, &parent_entries);
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

		int rows, cols;
		while (1) {
			erase();
			getmaxyx(stdscr, rows, cols);
			draw_frame(title);

			attron(COLOR_PAIR(COLOR_PAIR_NORMAL) | A_BOLD);
			mvprintw(1, 2, "%-*.*s", cols - 4, cols - 4, cwd);
			attroff(COLOR_PAIR(COLOR_PAIR_NORMAL) | A_BOLD);

			/* 3-column split: parent | current (has the cursor) | preview. */
			int content_top = 3, content_height = rows - 5;
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

			/* Preview column: child listing for a directory, size/mode
			 * for a file (not file contents — could be binary or huge). */
			attron(COLOR_PAIR(COLOR_PAIR_DIM));
			for (int row = 0; row < content_height; row++) {
				mvprintw(content_top + row, preview_x, "%-*s", preview_w, "");
			}
			attroff(COLOR_PAIR(COLOR_PAIR_DIM));
			if (count > 0) {
				if (entries[selected].is_dir) {
					char child_path[4096];
					snprintf(child_path, sizeof(child_path), "%s/%s", cwd, entries[selected].name);
					dir_entry *child_entries;
					int child_count = list_directory(child_path, &child_entries);
					if (child_count >= 0) {
						draw_column(content_top, preview_x, content_height, preview_w,
							child_entries, child_count, -1, 0, 1);
						free(child_entries);
					}
				} else {
					char size_str[32];
					human_size(entries[selected].size, size_str, sizeof(size_str));
					char mode_str[10];
					mode_string(entries[selected].mode, 0, mode_str);
					attron(COLOR_PAIR(COLOR_PAIR_DIM));
					mvprintw(content_top, preview_x, "Size: %s", size_str);
					mvprintw(content_top + 1, preview_x, "Mode: %s", mode_str);
					attroff(COLOR_PAIR(COLOR_PAIR_DIM));
				}
			}

			/* Vertical separators between columns. */
			attron(COLOR_PAIR(COLOR_PAIR_BORDER));
			for (int row = content_top; row < content_top + content_height; row++) {
				mvaddch(row, current_x - 1, ACS_VLINE);
				mvaddch(row, preview_x - 1, ACS_VLINE);
			}
			attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

			char info[64] = "";
			if (count > 0) {
				if (!entries[selected].is_dir) {
					char size_str[32];
					human_size(entries[selected].size, size_str, sizeof(size_str));
					snprintf(info, sizeof(info), "%d/%d  %s", selected + 1, count, size_str);
				} else {
					snprintf(info, sizeof(info), "%d/%d", selected + 1, count);
				}
			}
			draw_statusbar(rows, cols, "↑/↓ j/k move   Enter open/select   ⌫ up-dir   Esc/q cancel", info);
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
			case 27:
			case 'q':
				free(entries);
				free(parent_entries);
				return -1;
			case '\n':
			case KEY_ENTER:
				if (count == 0) {
					break;
				}
				if (entries[selected].is_dir) {
					char new_cwd[4096];
					snprintf(new_cwd, sizeof(new_cwd), "%s/%s", cwd, entries[selected].name);
					strncpy(cwd, new_cwd, sizeof(cwd) - 1);
					cwd[sizeof(cwd) - 1] = '\0';
					selected = 0;
					scroll_top = 0;
					free(entries);
					free(parent_entries);
					goto reload_dir;
				} else {
					snprintf(out, out_len, "%s/%s", cwd, entries[selected].name);
					free(entries);
					free(parent_entries);
					return 0;
				}
			default:
				break;
			}
		}
	reload_dir:
		continue;
	}
}

int syn_tui_password_prompt(const char *title, char *out, size_t out_len) {
	char buf[512] = {0};
	size_t len = 0;
	int rows, cols;

	curs_set(1);
	while (1) {
		erase();
		getmaxyx(stdscr, rows, cols);
		draw_frame(title);

		attron(COLOR_PAIR(COLOR_PAIR_NORMAL));
		mvprintw(rows / 2, 2, "> ");
		for (size_t i = 0; i < len; i++) {
			addch('*');
		}
		attroff(COLOR_PAIR(COLOR_PAIR_NORMAL));
		draw_statusbar(rows, cols, "Enter confirm   Esc cancel   Backspace delete", NULL);
		move(rows / 2, 4 + (int)len);
		refresh();

		int ch = getch();
		if (ch == '\n' || ch == KEY_ENTER) {
			curs_set(0);
			strncpy(out, buf, out_len - 1);
			out[out_len - 1] = '\0';
			memset(buf, 0, sizeof(buf));
			return 0;
		} else if (ch == 27) {
			curs_set(0);
			memset(buf, 0, sizeof(buf));
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

void syn_tui_message(const char *title, const char *body) {
	int rows, cols;
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

/* ------------------------------------------------------------------------
 *   Dashboard: one persistent screen — a sidebar of fields alongside a
 *   permanently visible Miller-column file browser — rather than the
 *   sequential syn_tui_menu()/syn_tui_file_picker()/
 *   syn_tui_password_prompt() calls used elsewhere in this file.
 * ------------------------------------------------------------------------ */

typedef enum {
	FIELD_ACTION = 0,
	FIELD_ALGORITHM,
	FIELD_FORMAT,       /* only meaningful for Redshirt + Encrypt */
	FIELD_KEY_OR_PASS,  /* label/behavior varies: password entry, or opens the file browser for an RSA key */
	FIELD_RUN,
	FIELD_COUNT
} sidebar_field;

static const char *ACTION_VALUES[] = {"Encrypt", "Decrypt"};
static const char *ALGO_VALUES[] = {"AES-256", "Blowfish", "RSA", "Redshirt"};
static const char *ALGO_KEYS[] = {"aes", "blowfish", "rsa", "redshirt"};
static const char *FORMAT_VALUES[] = {"REDSHIRT (v1)", "REDSHRT2 (v2)", "SYNX"};

typedef struct {
	int action_idx;      /* index into ACTION_VALUES */
	int algo_idx;         /* index into ALGO_VALUES/ALGO_KEYS */
	int format_idx;        /* index into FORMAT_VALUES, only used for redshirt+encrypt */
	char file[4096];
	char key_or_password[4096];
	int has_file;
	int has_key_or_password;
} dashboard_state;

/* Best-effort secure erase for the password buffer this file handles
 * internally — a local copy rather than pulling in syn_crypt.h/OpenSSL
 * just for this, since the TUI module has no other crypto dependency. */
static void syn_tui_wipe_buf(void *buf, size_t len) {
	volatile unsigned char *p = buf;
	while (len--) {
		*p++ = 0;
	}
}

/* True if the current algorithm needs a password (aes/blowfish) rather
 * than a key file (rsa) or nothing at all (redshirt). */
static int algo_needs_password(int algo_idx) {
	return algo_idx == 0 || algo_idx == 1; /* aes, blowfish */
}
static int algo_needs_keyfile(int algo_idx) {
	return algo_idx == 2; /* rsa */
}
static int algo_is_redshirt(int algo_idx) {
	return algo_idx == 3;
}

static int field_is_visible(const dashboard_state *st, sidebar_field f) {
	if (f == FIELD_FORMAT) {
		return algo_is_redshirt(st->algo_idx) && st->action_idx == 0; /* Encrypt only */
	}
	if (f == FIELD_KEY_OR_PASS) {
		return algo_needs_password(st->algo_idx) || algo_needs_keyfile(st->algo_idx);
	}
	return 1;
}

static int dashboard_is_ready(const dashboard_state *st) {
	if (!st->has_file) {
		return 0;
	}
	if (algo_needs_password(st->algo_idx) || algo_needs_keyfile(st->algo_idx)) {
		return st->has_key_or_password;
	}
	return 1;
}

/* Renders the left sidebar: one row per visible field, current value,
 * a Run row that lights up once every required field is filled in.
 * `sidebar_focused` dims the cursor highlight when focus is on the file
 * browser instead (Tab switches between the two). */
static void draw_sidebar(int width, int height, const dashboard_state *st, sidebar_field cursor, int sidebar_focused) {
	int row = 3; /* row 1 is the "SETTINGS" panel header, row 2 left blank */
	for (sidebar_field f = 0; f < FIELD_COUNT; f++) {
		if (!field_is_visible(st, f)) {
			continue;
		}
		int is_cursor = (f == cursor) && sidebar_focused;
		char line[256];
		char value[224] = "";

		/* Cyclable fields (Action/Algorithm/Format) show "◂ value ▸" only
		 * on the row the cursor is on. */
		const char *larrow = is_cursor ? "◂ " : "";
		const char *rarrow = is_cursor ? " ▸" : "";

		switch (f) {
		case FIELD_ACTION:
			snprintf(value, sizeof(value), "%s%s%s", larrow, ACTION_VALUES[st->action_idx], rarrow);
			snprintf(line, sizeof(line), "Action:    %s", value);
			break;
		case FIELD_ALGORITHM:
			snprintf(value, sizeof(value), "%s%s%s", larrow, ALGO_VALUES[st->algo_idx], rarrow);
			snprintf(line, sizeof(line), "Algorithm: %s", value);
			break;
		case FIELD_FORMAT:
			snprintf(value, sizeof(value), "%s%s%s", larrow, FORMAT_VALUES[st->format_idx], rarrow);
			snprintf(line, sizeof(line), "Format:    %s", value);
			break;
		case FIELD_KEY_OR_PASS:
			if (algo_needs_password(st->algo_idx)) {
				snprintf(line, sizeof(line), "Password:  %s",
					st->has_key_or_password ? "●●●●●●●●" : "(not set)");
			} else {
				snprintf(line, sizeof(line), "Key file:  %s",
					st->has_key_or_password ? st->key_or_password : "(not set)");
			}
			break;
		case FIELD_RUN:
			snprintf(line, sizeof(line), "%s", dashboard_is_ready(st) ? "▶ Run" : "  Run (incomplete)");
			break;
		default:
			line[0] = '\0';
			break;
		}

		int color = is_cursor ? COLOR_PAIR_SELECTED
			: (f == FIELD_RUN && dashboard_is_ready(st)) ? COLOR_PAIR_DIR
			: COLOR_PAIR_NORMAL;
		attron(COLOR_PAIR(color));
		mvprintw(row, 1, "%-*s", width - 1, "");
		mvprintw(row, 1, " %s", line);
		attroff(COLOR_PAIR(color));

		row++;
		if (f == FIELD_KEY_OR_PASS) {
			row++; /* blank separator before the Run row */
		}
	}

	/* File is set by the browser panel, not cycled like the fields
	 * above, so it isn't part of the FIELD_COUNT loop or cursor stops. */
	row++;
	attron(COLOR_PAIR(COLOR_PAIR_NORMAL) | A_BOLD);
	mvprintw(row, 1, "%-*s", width - 1, "");
	mvprintw(row, 1, " File:");
	attroff(COLOR_PAIR(COLOR_PAIR_NORMAL) | A_BOLD);
	row++;
	attron(COLOR_PAIR(st->has_file ? COLOR_PAIR_DIR : COLOR_PAIR_DIM));
	const char *file_display = st->has_file ? strrchr(st->file, '/') : NULL;
	file_display = file_display ? file_display + 1 : (st->has_file ? st->file : "(none — browse →)");
	mvprintw(row, 1, "%-*s", width - 1, "");
	mvprintw(row, 2, "%.*s", width - 3, file_display);
	attroff(COLOR_PAIR(st->has_file ? COLOR_PAIR_DIR : COLOR_PAIR_DIM));
}

syn_tui_dashboard_result syn_tui_dashboard(const char *start_dir) {
	dashboard_state st = {0};
	sidebar_field cursor = FIELD_ACTION;
	int sidebar_focused = 1; /* 1 = sidebar has keyboard focus, 0 = file browser does */

	char cwd[4096];
	strncpy(cwd, start_dir, sizeof(cwd) - 1);
	cwd[sizeof(cwd) - 1] = '\0';
	int browser_selected = 0;
	int browser_scroll_top = 0;

	syn_tui_dashboard_result result = {0};

	while (1) {
		dir_entry *entries;
		int count = list_directory(cwd, &entries);
		if (count < 0) {
			result.ok = 0;
			return result;
		}
		if (browser_selected >= count) {
			browser_selected = count > 0 ? count - 1 : 0;
		}

		char cwd_copy_for_parent[4096];
		strncpy(cwd_copy_for_parent, cwd, sizeof(cwd_copy_for_parent) - 1);
		cwd_copy_for_parent[sizeof(cwd_copy_for_parent) - 1] = '\0';
		char *parent_path = dirname(cwd_copy_for_parent);
		dir_entry *parent_entries = NULL;
		int parent_count = 0;
		int parent_sel = -1;
		if (strcmp(parent_path, cwd) != 0) {
			parent_count = list_directory(parent_path, &parent_entries);
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

		int rows, cols;
		int need_reload = 0;
		while (!need_reload) {
			erase();
			getmaxyx(stdscr, rows, cols);
			draw_frame("SYN-CRYPTER");

			int sidebar_w = cols / 4;
			if (sidebar_w < 22) {
				sidebar_w = 22;
			}

			/* Panel headers, marking which one currently has focus. */
			attron(COLOR_PAIR(sidebar_focused ? COLOR_PAIR_DIR : COLOR_PAIR_DIM) | A_BOLD);
			mvprintw(1, 1, "%-*s", sidebar_w - 1, sidebar_focused ? " SETTINGS (active)" : " SETTINGS");
			attroff(COLOR_PAIR(sidebar_focused ? COLOR_PAIR_DIR : COLOR_PAIR_DIM) | A_BOLD);

			draw_sidebar(sidebar_w, rows, &st, cursor, sidebar_focused);

			attron(COLOR_PAIR(COLOR_PAIR_BORDER));
			for (int row = 1; row < rows - 2; row++) {
				mvaddch(row, sidebar_w, ACS_VLINE);
			}
			attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

			int browser_x = sidebar_w + 2;
			int browser_top = 2, browser_height = rows - 4;
			int browser_total_w = cols - browser_x - 2;
			int parent_w = browser_total_w / 5;
			int preview_w = browser_total_w / 4;
			int current_x = browser_x + parent_w + 2;
			int current_w = browser_total_w - parent_w - preview_w - 4;
			int preview_x = current_x + current_w + 2;

			attron(COLOR_PAIR(sidebar_focused ? COLOR_PAIR_DIM : COLOR_PAIR_DIR) | A_BOLD);
			mvprintw(browser_top - 1, browser_x, "%-*s", browser_total_w, "");
			mvprintw(browser_top - 1, browser_x, "%s%.*s",
				sidebar_focused ? "FILES  " : "FILES (active)  ",
				browser_total_w - 20, cwd);
			attroff(COLOR_PAIR(sidebar_focused ? COLOR_PAIR_DIM : COLOR_PAIR_DIR) | A_BOLD);

			if (parent_count > 0) {
				draw_column(browser_top, browser_x, browser_height, parent_w,
					parent_entries, parent_count, parent_sel, 0, 1);
			}

			if (browser_selected < browser_scroll_top) {
				browser_scroll_top = browser_selected;
			}
			if (browser_selected >= browser_scroll_top + browser_height) {
				browser_scroll_top = browser_selected - browser_height + 1;
			}
			draw_column(browser_top, current_x, browser_height, current_w,
				entries, count, sidebar_focused ? -1 : browser_selected, browser_scroll_top, 0);

			attron(COLOR_PAIR(COLOR_PAIR_DIM));
			for (int row = 0; row < browser_height; row++) {
				mvprintw(browser_top + row, preview_x, "%-*s", preview_w, "");
			}
			attroff(COLOR_PAIR(COLOR_PAIR_DIM));
			if (count > 0) {
				if (entries[browser_selected].is_dir) {
					char child_path[4096];
					snprintf(child_path, sizeof(child_path), "%s/%s", cwd, entries[browser_selected].name);
					dir_entry *child_entries;
					int child_count = list_directory(child_path, &child_entries);
					if (child_count >= 0) {
						draw_column(browser_top, preview_x, browser_height, preview_w,
							child_entries, child_count, -1, 0, 1);
						free(child_entries);
					}
				} else {
					char size_str[32];
					human_size(entries[browser_selected].size, size_str, sizeof(size_str));
					char mode_str[10];
					mode_string(entries[browser_selected].mode, 0, mode_str);
					attron(COLOR_PAIR(COLOR_PAIR_DIM));
					mvprintw(browser_top, preview_x, "Size: %s", size_str);
					mvprintw(browser_top + 1, preview_x, "Mode: %s", mode_str);
					attroff(COLOR_PAIR(COLOR_PAIR_DIM));
				}
			}

			attron(COLOR_PAIR(COLOR_PAIR_BORDER));
			for (int row = browser_top; row < browser_top + browser_height; row++) {
				mvaddch(row, current_x - 1, ACS_VLINE);
				mvaddch(row, preview_x - 1, ACS_VLINE);
			}
			attroff(COLOR_PAIR(COLOR_PAIR_BORDER));

			const char *hint = sidebar_focused
				? "TAB: switch to file browser  |  UP/DOWN: pick a field  |  LEFT/RIGHT: change its value  |  ENTER: set / run  |  q: quit"
				: "TAB: switch to sidebar  |  UP/DOWN: move  |  ENTER: open folder / pick file  |  BACKSPACE: go up a folder  |  q: quit";
			draw_statusbar(rows, cols, hint, NULL);
			refresh();

			int ch = getch();

			if (ch == '\t') {
				sidebar_focused = !sidebar_focused;
				continue;
			}
			if (ch == 27 || ch == 'q') {
				free(entries);
				free(parent_entries);
				result.ok = 0;
				return result;
			}

			if (sidebar_focused) {
				switch (ch) {
				case KEY_UP:
				case 'k':
					do {
						cursor = (cursor - 1 + FIELD_COUNT) % FIELD_COUNT;
					} while (!field_is_visible(&st, cursor));
					break;
				case KEY_DOWN:
				case 'j':
					do {
						cursor = (cursor + 1) % FIELD_COUNT;
					} while (!field_is_visible(&st, cursor));
					break;
				case KEY_LEFT:
				case 'h':
					if (cursor == FIELD_ACTION) {
						st.action_idx = (st.action_idx + 1) % 2;
					} else if (cursor == FIELD_ALGORITHM) {
						st.algo_idx = (st.algo_idx + 3) % 4;
					} else if (cursor == FIELD_FORMAT) {
						st.format_idx = (st.format_idx + 2) % 3;
					}
					break;
				case KEY_RIGHT:
				case 'l':
					if (cursor == FIELD_ACTION) {
						st.action_idx = (st.action_idx + 1) % 2;
					} else if (cursor == FIELD_ALGORITHM) {
						st.algo_idx = (st.algo_idx + 1) % 4;
					} else if (cursor == FIELD_FORMAT) {
						st.format_idx = (st.format_idx + 1) % 3;
					}
					break;
				case '\n':
				case KEY_ENTER:
					if (cursor == FIELD_ACTION) {
						st.action_idx = (st.action_idx + 1) % 2;
					} else if (cursor == FIELD_ALGORITHM) {
						st.algo_idx = (st.algo_idx + 1) % 4;
					} else if (cursor == FIELD_FORMAT) {
						st.format_idx = (st.format_idx + 1) % 3;
					} else if (cursor == FIELD_KEY_OR_PASS) {
						if (algo_needs_password(st.algo_idx)) {
							char pw[4096];
							if (syn_tui_password_prompt(
									st.algo_idx == 0 ? "AES password" : "Blowfish password",
									pw, sizeof(pw)) == 0) {
								strncpy(st.key_or_password, pw, sizeof(st.key_or_password) - 1);
								st.has_key_or_password = 1;
							}
							syn_tui_wipe_buf(pw, sizeof(pw));
						} else if (algo_needs_keyfile(st.algo_idx)) {
							const char *key_title = st.action_idx == 0
								? "Choose a public key (.pem)" : "Choose a private key (.pem)";
							char key_path[4096];
							if (syn_tui_file_picker(key_title, cwd, key_path, sizeof(key_path)) == 0) {
								strncpy(st.key_or_password, key_path, sizeof(st.key_or_password) - 1);
								st.has_key_or_password = 1;
							}
						}
					} else if (cursor == FIELD_RUN) {
						if (dashboard_is_ready(&st)) {
							result.ok = 1;
							result.encrypt = (st.action_idx == 0);
							result.algo = ALGO_KEYS[st.algo_idx];
							result.redshirt_variant_index = st.format_idx;
							strncpy(result.file, st.file, sizeof(result.file) - 1);
							strncpy(result.key_or_password, st.key_or_password, sizeof(result.key_or_password) - 1);
							free(entries);
							free(parent_entries);
							return result;
						}
					}
					break;
				default:
					break;
				}
			} else {
				switch (ch) {
				case KEY_UP:
				case 'k':
					if (count > 0) browser_selected = (browser_selected - 1 + count) % count;
					break;
				case KEY_DOWN:
				case 'j':
					if (count > 0) browser_selected = (browser_selected + 1) % count;
					break;
				case KEY_BACKSPACE:
				case 127:
				case 8:
					free(entries);
					free(parent_entries);
					{
						char *cwd_copy = strdup(cwd);
						char *parent = dirname(cwd_copy);
						if (strcmp(parent, cwd) != 0) {
							strncpy(cwd, parent, sizeof(cwd) - 1);
							cwd[sizeof(cwd) - 1] = '\0';
							browser_selected = 0;
							browser_scroll_top = 0;
						}
						free(cwd_copy);
					}
					need_reload = 1;
					break;
				case '\n':
				case KEY_ENTER:
					if (count == 0) {
						break;
					}
					if (entries[browser_selected].is_dir) {
						char new_cwd[4096];
						snprintf(new_cwd, sizeof(new_cwd), "%s/%s", cwd, entries[browser_selected].name);
						strncpy(cwd, new_cwd, sizeof(cwd) - 1);
						cwd[sizeof(cwd) - 1] = '\0';
						browser_selected = 0;
						browser_scroll_top = 0;
						free(entries);
						free(parent_entries);
						need_reload = 1;
					} else {
						snprintf(st.file, sizeof(st.file), "%s/%s", cwd, entries[browser_selected].name);
						st.has_file = 1;
						/* Return focus to the sidebar, on whichever field
						 * still needs attention. */
						sidebar_focused = 1;
						if (algo_needs_password(st.algo_idx) || algo_needs_keyfile(st.algo_idx)) {
							cursor = FIELD_KEY_OR_PASS;
						} else {
							cursor = FIELD_RUN;
						}
					}
					break;
				default:
					break;
				}
			}
		}
		free(entries);
		free(parent_entries);
	}
}
