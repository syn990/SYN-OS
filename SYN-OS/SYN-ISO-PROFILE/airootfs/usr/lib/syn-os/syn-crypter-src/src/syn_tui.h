/* ------------------------------------------------------------------------
 *   Small ncurses widget set backing syn-crypter's interactive mode:
 *   an arrow-key list menu, a directory browser (Enter descends/selects,
 *   Backspace goes up — same shape as ranger/btop's navigation, not a
 *   rofi popup), and a masked password prompt. Colored from the live
 *   SYN-OS theme (see syn_theme.h), not a hardcoded palette.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CRYPTER (Security)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TUI_H
#define SYN_TUI_H

#include <stddef.h>

/* Call once before any other syn_tui_* function; syn_tui_end() to leave
 * ncurses mode (e.g. before printing a final result to a plain terminal). */
void syn_tui_init(void);
void syn_tui_end(void);

/* Arrow-key/j-k list menu. `title` is shown above the list, `items` is an
 * array of `count` null-terminated strings. Returns the chosen index, or
 * -1 if the user cancelled (Esc/q). */
int syn_tui_menu(const char *title, const char *const *items, int count);

/* Interactive directory browser rooted initially at `start_dir`. Enter on
 * a directory descends into it; Enter on a regular file selects it and
 * returns its full path in `out` (caller-owned buffer, `out_len` bytes).
 * Backspace/".." goes up a directory. Esc/q cancels (returns -1). Returns
 * 0 on a successful file selection. `title` is shown in the frame, e.g.
 * "Choose a file" vs. "Choose a public key (.pem)". */
int syn_tui_file_picker(const char *title, const char *start_dir, char *out, size_t out_len);

/* Masked line-input prompt (stars for each typed character). Returns 0
 * and fills `out` on Enter, -1 on Esc (out left untouched). */
int syn_tui_password_prompt(const char *title, char *out, size_t out_len);

/* Centered message screen with "press any key to continue". Used for the
 * final success/failure report before returning to a plain terminal. */
void syn_tui_message(const char *title, const char *body);

/* Result of one syn_tui_dashboard() run — every field the caller needs to
 * build a syn-crypter operation, or ok=0 if the user quit (q/Esc from the
 * sidebar) without confirming Run. */
typedef struct {
	int ok;
	int encrypt;                   /* 1 = Encrypt, 0 = Decrypt */
	const char *algo;               /* "aes" | "blowfish" | "rsa" | "redshirt" */
	int redshirt_variant_index;     /* 0/1/2 — REDSHIRT/REDSHRT2/SYNX; meaningless unless algo is redshirt+encrypt */
	char file[4096];
	char key_or_password[4096];     /* password for aes/blowfish, PEM path for rsa, unused for redshirt */
} syn_tui_dashboard_result;

/* The persistent dashboard: a left sidebar (Action, Algorithm, Format,
 * Key/Password, a Run row) alongside a Miller-column file browser in the
 * main panel. Tab switches focus between the two; arrow keys/Enter
 * operate whichever has focus. `start_dir` seeds the file browser's
 * initial directory (typically $HOME). */
syn_tui_dashboard_result syn_tui_dashboard(const char *start_dir);

#endif
