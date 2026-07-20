/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CRYPTER (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_theme.h"

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* SYN-OS-RED's shipped values (see DotfileOverlay's theme-templates) —
 * used whenever the active theme can't be read at all, so the TUI always
 * has a sane palette rather than falling back to ncurses' raw defaults. */
static const syn_palette FALLBACK = {
	.bg          = {0x00, 0x00, 0x00},
	.panel       = {0x2c, 0x01, 0x01},
	.panel_hover = {0x40, 0x01, 0x01},
	.accent      = {0x80, 0x00, 0x00},
	.accent_dim  = {0x26, 0x01, 0x01},
	.text        = {0xf8, 0xf8, 0xf2},
	.border      = {0x44, 0x44, 0x44},
	.urgent      = {0xff, 0x55, 0x55},
};

static int parse_hex_color(const char *hex, syn_rgb *out) {
	if (hex[0] != '#') {
		return -1;
	}
	unsigned int r, g, b;
	if (sscanf(hex + 1, "%2x%2x%2x", &r, &g, &b) != 3) {
		return -1;
	}
	out->r = (short)r;
	out->g = (short)g;
	out->b = (short)b;
	return 0;
}

/* Strips a KEY="value" or KEY=value line down to just `value`, handling
 * the double-quoted form every .theme file actually uses. Returns NULL
 * if `line` doesn't start with `key=`. */
static const char *extract_value(const char *line, const char *key, char *buf, size_t buflen) {
	size_t key_len = strlen(key);
	if (strncmp(line, key, key_len) != 0 || line[key_len] != '=') {
		return NULL;
	}
	const char *v = line + key_len + 1;
	if (*v == '"') {
		v++;
	}
	size_t i = 0;
	while (v[i] && v[i] != '"' && v[i] != '\n' && i < buflen - 1) {
		buf[i] = v[i];
		i++;
	}
	buf[i] = '\0';
	return buf;
}

void syn_theme_load(syn_palette *out) {
	*out = FALLBACK;

	const char *home = getenv("HOME");
	if (!home) {
		return;
	}

	char current_theme_path[4096];
	snprintf(current_theme_path, sizeof(current_theme_path), "%s/.config/syn-os/current-theme", home);

	char theme_name[256] = "SYN-OS-RED";
	FILE *cf = fopen(current_theme_path, "r");
	if (cf) {
		if (fgets(theme_name, sizeof(theme_name), cf)) {
			size_t len = strlen(theme_name);
			while (len > 0 && (theme_name[len - 1] == '\n' || theme_name[len - 1] == '\r')) {
				theme_name[--len] = '\0';
			}
		}
		fclose(cf);
	}

	char theme_path[4096];
	snprintf(theme_path, sizeof(theme_path), "%s/.config/syn-os/themes/%s.theme", home, theme_name);

	FILE *f = fopen(theme_path, "r");
	if (!f) {
		return;
	}

	char line[1024];
	char value[512];
	while (fgets(line, sizeof(line), f)) {
		syn_rgb color;
		const char *v;
		if ((v = extract_value(line, "SYN_BG", value, sizeof(value))) && parse_hex_color(v, &color) == 0) {
			out->bg = color;
		} else if ((v = extract_value(line, "SYN_PANEL_HOVER", value, sizeof(value))) && parse_hex_color(v, &color) == 0) {
			out->panel_hover = color;
		} else if ((v = extract_value(line, "SYN_PANEL", value, sizeof(value))) && parse_hex_color(v, &color) == 0) {
			out->panel = color;
		} else if ((v = extract_value(line, "SYN_ACCENT_DIM", value, sizeof(value))) && parse_hex_color(v, &color) == 0) {
			out->accent_dim = color;
		} else if ((v = extract_value(line, "SYN_ACCENT", value, sizeof(value))) && parse_hex_color(v, &color) == 0) {
			out->accent = color;
		} else if ((v = extract_value(line, "SYN_TEXT", value, sizeof(value))) && parse_hex_color(v, &color) == 0) {
			out->text = color;
		} else if ((v = extract_value(line, "SYN_BORDER", value, sizeof(value))) && parse_hex_color(v, &color) == 0) {
			out->border = color;
		} else if ((v = extract_value(line, "SYN_URGENT", value, sizeof(value))) && parse_hex_color(v, &color) == 0) {
			out->urgent = color;
		}
	}
	fclose(f);
}

static short scale(short v) { return (short)((int)v * 1000 / 255); }

void syn_theme_apply_curses_colors(void) {
	syn_palette pal;
	syn_theme_load(&pal);
	use_default_colors();
	init_color(16, scale(pal.bg.r), scale(pal.bg.g), scale(pal.bg.b));
	init_color(17, scale(pal.text.r), scale(pal.text.g), scale(pal.text.b));
	init_color(18, scale(pal.accent.r), scale(pal.accent.g), scale(pal.accent.b));
	init_color(19, scale(pal.panel.r), scale(pal.panel.g), scale(pal.panel.b));
	init_color(20, scale(pal.border.r), scale(pal.border.g), scale(pal.border.b));
	init_pair(SYN_THEME_PAIR_NORMAL, 17, -1);
	init_pair(SYN_THEME_PAIR_SELECTED, 16, 18);
	init_pair(SYN_THEME_PAIR_TITLE, 18, -1);
	init_pair(SYN_THEME_PAIR_BORDER, 19, -1);
	init_pair(SYN_THEME_PAIR_DIM, 20, -1);
	init_pair(SYN_THEME_PAIR_STATUSBAR, 16, 18);
	bkgd(COLOR_PAIR(SYN_THEME_PAIR_NORMAL));
}
