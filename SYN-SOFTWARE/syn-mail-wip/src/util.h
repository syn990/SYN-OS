/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_UTIL_H
#define SYN_MAIL_UTIL_H
#include <stddef.h>
#include <stdint.h>

/* a growable byte buffer, always NUL-terminated */
typedef struct {
	char *p;
	size_t n, cap;
} Buf;

void buf_put(Buf *b, const void *p, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void buf_free(Buf *b);
/* wipe then free: for anything that held a secret */
void buf_wipe(Buf *b);

void b64_encode(const void *in, size_t n, Buf *out);
/* decodes standard base64, skipping whitespace; returns bytes written or -1 */
long b64_decode(const char *in, size_t n, Buf *out);

void str_copy(char *dst, size_t n, const char *src);
int str_ieq_prefix(const char *s, const char *prefix);

/* one code point from s (n bytes left); returns the bytes used, 0 at the end; bad bytes give U+FFFD and 1 */
size_t utf8_next(const char *s, size_t n, uint32_t *cp);
void utf8_put(Buf *b, uint32_t cp);
#endif
