/* ------------------------------------------------------------------------
 *   A centered modal box drawn on top of whatever's already on screen —
 *   unlike syn-crypter-src's syn_tui_password_prompt, which takes over
 *   the full screen (erase() + draw_frame() against stdscr directly).
 *   Used for the doas password popup: the commit browser/build
 *   confirmation stays visible underneath, matching the "Uplink-style
 *   popup" the user asked for rather than a full-screen takeover.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TUI_MODAL_H
#define SYN_TUI_MODAL_H

#include <stddef.h>

/* Masked password entry in a small centered box. Whatever's already
 * drawn on stdscr stays visible around the box. Returns 0 and fills
 * `out` on Enter, -1 on Esc (out left untouched either way beyond
 * being NUL-terminated on success). */
int syn_tui_modal_password_prompt(const char *title, char *out, size_t out_len);

#endif
