/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "util.h"

#include <ctype.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void grow(Buf *b, size_t need) {
	if (b->n + need + 1 <= b->cap) {
		return;
	}
	size_t cap = b->cap ? b->cap : 256;
	while (cap < b->n + need + 1) {
		cap *= 2;
	}
	char *p = realloc(b->p, cap);
	if (!p) {
		abort();
	}
	b->p = p;
	b->cap = cap;
}

void buf_put(Buf *b, const void *p, size_t n) {
	grow(b, n);
	memcpy(b->p + b->n, p, n);
	b->n += n;
	b->p[b->n] = 0;
}

void buf_puts(Buf *b, const char *s) {
	buf_put(b, s, strlen(s));
}

void buf_printf(Buf *b, const char *fmt, ...) {
	va_list ap, aq;
	va_start(ap, fmt);
	va_copy(aq, ap);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n > 0) {
		grow(b, (size_t)n);
		vsnprintf(b->p + b->n, (size_t)n + 1, fmt, aq);
		b->n += (size_t)n;
	}
	va_end(aq);
}

void buf_free(Buf *b) {
	free(b->p);
	*b = (Buf){0};
}

void buf_wipe(Buf *b) {
	if (b->p) {
		OPENSSL_cleanse(b->p, b->cap);
	}
	buf_free(b);
}

void b64_encode(const void *in, size_t n, Buf *out) {
	size_t cap = 4 * ((n + 2) / 3) + 1;
	grow(out, cap);
	int m = EVP_EncodeBlock((unsigned char *)out->p + out->n, in, (int)n);
	out->n += (size_t)m;
	out->p[out->n] = 0;
}

static int b64v(int c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+' || c == '-') return 62;
	if (c == '/' || c == '_') return 63;
	return -1;
}

long b64_decode(const char *in, size_t n, Buf *out) {
	unsigned acc = 0;
	int bits = 0;
	long w = 0;
	for (size_t i = 0; i < n; i++) {
		int c = (unsigned char)in[i];
		if (c == '=') {
			break;
		}
		if (isspace(c)) {
			continue;
		}
		int v = b64v(c);
		if (v < 0) {
			return -1;
		}
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			char ch = (char)((acc >> bits) & 0xff);
			buf_put(out, &ch, 1);
			w++;
		}
	}
	return w;
}

void str_copy(char *dst, size_t n, const char *src) {
	if (n == 0) {
		return;
	}
	snprintf(dst, n, "%s", src ? src : "");
}

int str_ieq_prefix(const char *s, const char *prefix) {
	return strncasecmp(s, prefix, strlen(prefix)) == 0;
}
