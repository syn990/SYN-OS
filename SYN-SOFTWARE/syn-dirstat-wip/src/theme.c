/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "theme.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"

/* KEY="value" or KEY=value: the value in buf, or NULL if the line is not that key. */
static const char *value_of(const char *line, const char *key, char *buf, size_t len) {
	size_t n = strlen(key);
	if (strncmp(line, key, n) != 0 || line[n] != '=') {
		return NULL;
	}
	const char *v = line + n + 1;
	if (*v == '"') {
		v++;
	}
	size_t i = 0;
	while (v[i] && v[i] != '"' && v[i] != '\n' && v[i] != '\r' && i < len - 1) {
		buf[i] = v[i];
		i++;
	}
	buf[i] = '\0';
	return buf;
}

static void colour(const char *hex, Rgb *out) {
	unsigned r, g, b;
	if (hex[0] == '#' && sscanf(hex + 1, "%2x%2x%2x", &r, &g, &b) == 3) {
		*out = (Rgb){(unsigned char)r, (unsigned char)g, (unsigned char)b};
	}
}

static int by_name(const void *a, const void *b) {
	return strcmp(((const Theme *)a)->name, ((const Theme *)b)->name);
}

int themes_load(Theme **out) {
	/* longer keys first, so SYN_PANEL does not take SYN_PANEL_HOVER's line */
	static const struct {
		const char *key;
		size_t at;
	} COLOURS[] = {
		{"SYN_BG_ALT", offsetof(Theme, bg_alt)},
		{"SYN_BG", offsetof(Theme, bg)},
		{"SYN_PANEL_HOVER", offsetof(Theme, panel_hover)},
		{"SYN_PANEL", offsetof(Theme, panel)},
		{"SYN_ACCENT_DIM", offsetof(Theme, accent_dim)},
		{"SYN_ACCENT", offsetof(Theme, accent)},
		{"SYN_TEXT", offsetof(Theme, text)},
		{"SYN_BORDER", offsetof(Theme, border)},
		{"SYN_URGENT", offsetof(Theme, urgent)},
	};
	Theme *list = NULL;
	int count = 0;
	/* the theme files one after another: each SYN_THEME_NAME line starts a theme */
	for (const char *p = ASSET_THEMES; *p;) {
		char line[512], v[256];
		size_t n = strcspn(p, "\n");
		snprintf(line, sizeof line, "%.*s", (int)n, p);
		p += n + (p[n] == '\n');
		if (value_of(line, "SYN_THEME_NAME", v, sizeof v)) {
			list = realloc(list, (size_t)(count + 1) * sizeof *list);
			if (!list) {
				abort();
			}
			memset(&list[count], 0, sizeof *list);
			snprintf(list[count].name, sizeof list->name, "%.63s", v);
			count++;
			continue;
		}
		if (!count) {
			continue;
		}
		Theme *t = &list[count - 1];
		if (value_of(line, "SYN_THEME_MODE", v, sizeof v)) {
			t->light = !strcmp(v, "light");
		}
		for (size_t i = 0; i < sizeof COLOURS / sizeof *COLOURS; i++) {
			if (value_of(line, COLOURS[i].key, v, sizeof v)) {
				colour(v, (Rgb *)((char *)t + COLOURS[i].at));
				break;
			}
		}
	}
	qsort(list, (size_t)count, sizeof *list, by_name);
	*out = list;
	return count;
}

static int find(const Theme *list, int count, const char *name) {
	for (int i = 0; i < count; i++) {
		if (!strcmp(list[i].name, name)) {
			return i;
		}
	}
	return -1;
}

int theme_start(const Theme *list, int count, const char *asked) {
	if (asked) {
		return find(list, count, asked);
	}
	char name[128] = "SYN-OS-RED", path[1024];
	const char *home = getenv("HOME");
	snprintf(path, sizeof path, "%s/.config/syn-os/current-theme", home ? home : "");
	FILE *f = fopen(path, "r");
	if (f) {
		if (fgets(name, sizeof name, f)) {
			name[strcspn(name, "\r\n")] = '\0';
		}
		fclose(f);
	}
	int i = find(list, count, name);
	return i >= 0 ? i : find(list, count, "SYN-OS-RED");
}
