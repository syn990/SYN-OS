/* ------------------------------------------------------------------------
 *   Minimal flat-menu and message-screen widgets — same shape as
 *   syn-crypter-src's syn_tui_menu/syn_tui_message, duplicated here
 *   rather than cross-tree-linked (this repo's established convention:
 *   syn_theme.c is copied per-tree, not shared via a library) since
 *   syn-crypter-src's draw_frame/draw_statusbar helpers are static and
 *   not exported.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TUI_BASIC_H
#define SYN_TUI_BASIC_H

#include <stddef.h>

void syn_tui_init(void);
void syn_tui_end(void);

/* ncurses only notices a SIGWINCH-driven terminal resize "when reading
 * input data" (resizeterm(3X)) — normally via getch(). Any screen that
 * might draw right after a stretch with no getch() call in between
 * (e.g. syn_tui_buildlog's redraw loop, driven by a plain select()/
 * read() over a pty, not ncurses input) can still have stale
 * LINES/COLS at that point even though the real terminal already
 * resized — confirmed live: drawing at the stale size while the
 * physical terminal was already the new one produced a garbled overlap
 * of old and new content that persisted into the NEXT screen drawn
 * too (syn_tui_message() call right after also inherited the same
 * staleness), not just the screen where the resize happened. Call this
 * at the top of any such screen to force ncurses to reconcile its idea
 * of the terminal size with reality first. */
void syn_tui_sync_term_size(void);

/* Arrow-key/j-k flat list menu. Returns the chosen index, or -1 on
 * Esc/q. */
int syn_tui_menu(const char *title, const char *const *items, int count);

/* Centered message, "press any key to continue". */
void syn_tui_message(const char *title, const char *body);

/* Single-line text input (unmasked), reused for "enter a repo URL" /
 * "enter a local profile path" prompts. Returns 0 and fills `out` on
 * Enter, -1 on Esc. `initial` pre-fills the buffer (may be ""). */
int syn_tui_text_prompt(const char *title, const char *initial, char *out, size_t out_len);

#endif
