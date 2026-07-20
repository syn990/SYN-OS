/* ------------------------------------------------------------------------
 *   Reads the active SYN-OS theme's SYN_* hex palette directly (no shell
 *   involved) so the ncurses TUI matches whatever theme is live, instead
 *   of hardcoding its own colors — same palette rofi's -theme-str calls
 *   already read from these same files via syn-theme-lib.zsh.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CRYPTER (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_THEME_H
#define SYN_THEME_H

typedef struct {
	short r, g, b; /* 0-255 */
} syn_rgb;

typedef struct {
	syn_rgb bg;
	syn_rgb panel;
	syn_rgb panel_hover;
	syn_rgb accent;
	syn_rgb accent_dim;
	syn_rgb text;
	syn_rgb border;
	syn_rgb urgent;
} syn_palette;

/* Fills *out with the active theme's palette, falling back to SYN-OS-RED's
 * shipped defaults for any field it can't read (missing theme file,
 * missing key, or no $HOME) — mirrors syn-theme-lib.zsh's own
 * ${SYN_X:-fallback} behavior for callers that need one variable. */
void syn_theme_load(syn_palette *out);

/* Fixed color pair IDs every syn-os ncurses TUI shares — set up by
 * syn_theme_apply_curses_colors() below. Callers needing more pairs of
 * their own (e.g. syn-crypter's directory-name color) start at
 * SYN_THEME_PAIR_COUNT. */
enum {
	SYN_THEME_PAIR_NORMAL = 1,
	SYN_THEME_PAIR_SELECTED,
	SYN_THEME_PAIR_TITLE,
	SYN_THEME_PAIR_BORDER,
	SYN_THEME_PAIR_DIM,
	SYN_THEME_PAIR_STATUSBAR,
	SYN_THEME_PAIR_COUNT
};

/* Custom color slots 16-20 (0-15 stay the standard ANSI palette every
 * terminal expects to still mean what they normally mean), loaded from the
 * live theme via init_color, and the SYN_THEME_PAIR_* pairs above defined
 * from them. -1 (ncurses' "terminal default") is used for every pair's
 * background so a transparent host terminal (foot's own alpha) shows
 * through instead of an opaque painted rectangle. Must be called after
 * start_color(); safe to call even if has_colors() is false (init_color/
 * init_pair just become no-ops on such terminals). Slots 16-20 remain
 * defined afterward for callers that want to build additional pairs of
 * their own on top (e.g. slot 18 is the theme's accent color). */
void syn_theme_apply_curses_colors(void);

#endif
