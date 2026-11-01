/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_MIME_H
#define SYN_MAIL_MIME_H
#include "util.h"

/* Reading mail: header lookup with unfolding and RFC 2047 words decoded to UTF-8, and the readable text of a message
 * (the first text/plain part, else text/html with its tags taken out), its transfer encoding and charset undone. */

/* the header named name (case-insensitive) from a header block, decoded; 0 if found */
int mime_header(const char *msg, size_t n, const char *name, Buf *out);
/* RFC 2047 encoded words in s decoded to UTF-8 */
void mime_words(const char *s, Buf *out);
/* the readable text of a whole message */
void mime_text(const char *msg, size_t n, Buf *out);
#endif
