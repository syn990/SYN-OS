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

/* The HTML of a message as it was made (G265): the first text/html part that is not an attachment, in UTF-8, and
 * every part that carries a Content-ID (the images an HTML part names as cid:...), their bytes decoded. */
#define MIME_MAX_PARTS 64
typedef struct {
	char cid[256];  /* the Content-ID without its angle brackets */
	char type[96];  /* image/png, lower case, parameters dropped */
	Buf data;
} MimePart;

typedef struct {
	int found;
	Buf html;
	MimePart *part;
	int np;
} MimeHtml;

/* 0 when the message has an HTML part; out is filled either way and freed with mime_html_free */
int mime_html(const char *msg, size_t n, MimeHtml *out);
void mime_html_free(MimeHtml *h);
#endif
