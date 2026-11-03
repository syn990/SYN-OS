/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_LETTER_H
#define SYN_MAIL_LETTER_H
#include "util.h"

/* who signs a letter: name (the account's display name, else its address), an optional title, the company and the
 * address the reply comes back to; empty or NULL fields are left out of the block */
typedef struct {
	const char *name;
	const char *title;
	const char *company;
	const char *address;
} MsgSign;

/* the company the letters are signed for, and the notice every letter ends with (the same words as SYN-CASTER's
 * fn_mail_disclaimer in src/fn_records.c: change both together) */
extern const char LETTER_COMPANY[];
extern const char LETTER_NOTICE[];

/* the pinned company look: SYN-OS-M141, written here, never read from the desktop's current theme, so every letter
 * looks the same whatever the desktop wears */
extern const char LETTER_THEME[];

/* the text twin: the body as it is (CR dropped), then "-- ", the signature lines and the notice */
void letter_text(const char *body, const MsgSign *s, Buf *out);
/* the rendered letter: one HTML document, tables and inline styles only (Gmail drops <style> in some views), no
 * scripts, no remote images, no links made from the text; the body escaped, a blank line a new paragraph, a line break
 * a <br>; then the signature block and the company footer with the notice */
void letter_html(const char *subject, const char *body, const MsgSign *s, Buf *out);
#endif
