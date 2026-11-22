/* ------------------------------------------------------------------------
 *   SYN-OS themes: the same .theme palette files labwc, waybar and foot
 *   are generated from. They are built into the program (assets.c, made
 *   from the ISO profile's themes when it is compiled), so it wears them
 *   on Windows too.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SHELL_SPACE_THEME_H
#define SYN_SHELL_SPACE_THEME_H

#include <stdbool.h>

typedef struct {
	unsigned char r, g, b;
} Rgb;

typedef struct {
	char name[64]; /* SYN_THEME_NAME, e.g. SYN-OS-HALO-CYAN */
	bool light;
	Rgb bg, bg_alt, panel, panel_hover, accent, accent_dim, text, border, urgent;
} Theme;

/* Every built-in theme, in name order; returns how many. */
int themes_load(Theme **out);

/* Which theme to start in: the one asked for (-1 if there is none by that
 * name), or with asked NULL the one SYN-OS has applied
 * (~/.config/syn-os/current-theme), or SYN-OS-RED where that is not set. */
int theme_start(const Theme *list, int count, const char *asked);

#endif
