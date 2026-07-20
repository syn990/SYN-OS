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

#endif
