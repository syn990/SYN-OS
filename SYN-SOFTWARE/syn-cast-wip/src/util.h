/* ------------------------------------------------------------------------
 *   Small shared helpers: clocks, logging, base64, UTF-8, paths and files.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_UTIL_H
#define SYN_CAST_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

int64_t now_ms(void);      /* CLOCK_MONOTONIC */
int64_t wall_ms(void);     /* CLOCK_REALTIME, unix epoch */

void logf_(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define LOG(...) logf_(__VA_ARGS__)

/* Standard base64 (with padding) or base64url (no padding). Returns a
 * malloc'd NUL-terminated string. */
char *b64_encode(const uint8_t *in, size_t n, bool url);
/* Decodes standard or url-safe base64, padding optional. Returns bytes
 * written, or -1 on a malformed input or a too-small output. */
long b64_decode(const char *in, uint8_t *out, size_t out_cap);

/* Decodes one UTF-8 sequence at s (n bytes available). Writes the code
 * point (U+FFFD on a malformed sequence) and returns bytes consumed (>=1). */
size_t utf8_next(const char *s, size_t n, uint32_t *cp);
/* Bytes of the UTF-8 character before position i (>=1, or 0 at i==0). */
size_t utf8_prev_len(const char *s, size_t i);

/* "~/x" -> "$HOME/x". Writes into out, returns out. */
char *expand_home(const char *path, char *out, size_t cap);
int mkdir_p(const char *path, unsigned mode);
/* Reads a whole file; returns malloc'd buffer (NUL-terminated) or NULL. */
uint8_t *read_file(const char *path, size_t *len, size_t max);
int write_file(const char *path, const void *data, size_t len, unsigned mode);

/* Copies at most cap-1 bytes and always terminates. */
void str_copy(char *dst, size_t cap, const char *src);

/* Runs argv detached (double fork), stdio to /dev/null. */
void spawn_detached(char *const argv[]);

#endif
