/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "mime.h"

#include <ctype.h>
#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define MIME_DEPTH 8

/* where the body starts: after the first blank line */
static size_t body_at(const char *m, size_t n) {
	if (n >= 2 && m[0] == '\r' && m[1] == '\n') {
		return 2;
	}
	if (n >= 1 && m[0] == '\n') {
		return 1;
	}
	for (size_t i = 0; i + 1 < n; i++) {
		if (m[i] != '\n') {
			continue;
		}
		if (m[i + 1] == '\n') {
			return i + 2;
		}
		if (m[i + 1] == '\r' && i + 2 < n && m[i + 2] == '\n') {
			return i + 3;
		}
	}
	return n;
}

static void to_utf8(const char *charset, const char *s, size_t n, Buf *out) {
	if (!charset || !*charset || !strcasecmp(charset, "utf-8") || !strcasecmp(charset, "utf8") ||
		!strcasecmp(charset, "us-ascii")) {
		buf_put(out, s, n);
		return;
	}
	iconv_t cd = iconv_open("UTF-8//TRANSLIT", charset);
	if (cd == (iconv_t)-1) {
		buf_put(out, s, n);
		return;
	}
	char tmp[4096];
	char *in = (char *)s;
	size_t inl = n;
	while (inl > 0) {
		char *o = tmp;
		size_t ol = sizeof tmp;
		size_t r = iconv(cd, &in, &inl, &o, &ol);
		buf_put(out, tmp, (size_t)(o - tmp));
		if (r == (size_t)-1 && errno != E2BIG) {
			buf_puts(out, "?");
			in++;
			inl--;
		}
	}
	iconv_close(cd);
}

static int hexv(int c) {
	if (c >= '0' && c <= '9') return c - '0';
	c = toupper(c);
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* quoted-printable; in_header: '_' is a space (RFC 2047 Q) */
static void qp_decode(const char *s, size_t n, int in_header, Buf *out) {
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (c == '=' && i + 1 < n && (s[i + 1] == '\n' || s[i + 1] == '\r')) {
			i += (s[i + 1] == '\r' && i + 2 < n && s[i + 2] == '\n') ? 2 : 1;
			continue;
		}
		if (c == '=' && i + 2 < n && hexv(s[i + 1]) >= 0 && hexv(s[i + 2]) >= 0) {
			char ch = (char)(hexv(s[i + 1]) * 16 + hexv(s[i + 2]));
			buf_put(out, &ch, 1);
			i += 2;
			continue;
		}
		if (in_header && c == '_') {
			c = ' ';
		}
		buf_put(out, &c, 1);
	}
}

void mime_words(const char *s, Buf *out) {
	const char *p = s;
	int last_word = 0;
	while (*p) {
		const char *w = strstr(p, "=?");
		if (!w) {
			buf_puts(out, p);
			return;
		}
		const char *q1 = strchr(w + 2, '?');
		const char *q2 = q1 ? strchr(q1 + 1, '?') : NULL;
		const char *end = q2 ? strstr(q2 + 1, "?=") : NULL;
		if (!q1 || !q2 || !end || q2 - q1 != 2) {
			buf_put(out, p, (size_t)(w - p) + 2);
			p = w + 2;
			last_word = 0;
			continue;
		}
		/* whitespace between two encoded words is dropped (RFC 2047 section 6.2) */
		int only_space = 1;
		for (const char *t = p; t < w; t++) {
			if (!isspace((unsigned char)*t)) {
				only_space = 0;
			}
		}
		if (!(last_word && only_space)) {
			buf_put(out, p, (size_t)(w - p));
		}
		char cs[40];
		size_t cl = (size_t)(q1 - w - 2);
		if (cl >= sizeof cs) {
			cl = sizeof cs - 1;
		}
		memcpy(cs, w + 2, cl);
		cs[cl] = 0;
		char *star = strchr(cs, '*'); /* RFC 2231 language tag */
		if (star) {
			*star = 0;
		}
		Buf raw = {0};
		if (toupper((unsigned char)q1[1]) == 'B') {
			b64_decode(q2 + 1, (size_t)(end - q2 - 1), &raw);
		} else {
			qp_decode(q2 + 1, (size_t)(end - q2 - 1), 1, &raw);
		}
		to_utf8(cs, raw.p ? raw.p : "", raw.n, out);
		buf_free(&raw);
		p = end + 2;
		last_word = 1;
	}
}

/* the raw (unfolded, undecoded) value of a header within the header block m[0..n) */
static int header_raw(const char *m, size_t n, const char *name, Buf *out) {
	size_t hl = body_at(m, n), nl = strlen(name);
	for (size_t i = 0; i < hl;) {
		size_t e = i;
		while (e < hl && m[e] != '\n') {
			e++;
		}
		if (e - i > nl && strncasecmp(m + i, name, nl) == 0 && m[i + nl] == ':') {
			size_t v = i + nl + 1;
			for (;;) {
				size_t ve = e;
				while (ve > v && (m[ve - 1] == '\r')) {
					ve--;
				}
				while (v < ve && (m[v] == ' ' || m[v] == '\t')) {
					v++;
				}
				if (out->n) {
					buf_puts(out, " ");
				}
				buf_put(out, m + v, ve - v);
				if (e + 1 < hl && (m[e + 1] == ' ' || m[e + 1] == '\t')) {
					v = e + 1;
					e = v;
					while (e < hl && m[e] != '\n') {
						e++;
					}
					continue;
				}
				break;
			}
			if (!out->p) {
				buf_puts(out, "");
			}
			return 0;
		}
		i = e + 1;
	}
	return -1;
}

int mime_header(const char *msg, size_t n, const char *name, Buf *out) {
	Buf raw = {0};
	if (header_raw(msg, n, name, &raw) != 0) {
		buf_free(&raw);
		return -1;
	}
	mime_words(raw.p, out);
	buf_free(&raw);
	return 0;
}

/* a parameter (charset, boundary) from a Content-Type value */
static void param(const char *v, const char *k, char *out, size_t on) {
	out[0] = 0;
	size_t kl = strlen(k);
	for (const char *p = strchr(v, ';'); p; p = strchr(p + 1, ';')) {
		const char *s = p + 1;
		while (isspace((unsigned char)*s)) {
			s++;
		}
		if (strncasecmp(s, k, kl) == 0 && s[kl] == '=') {
			s += kl + 1;
			size_t w = 0;
			if (*s == '"') {
				s++;
				while (*s && *s != '"' && w + 1 < on) {
					out[w++] = *s++;
				}
			} else {
				while (*s && *s != ';' && !isspace((unsigned char)*s) && w + 1 < on) {
					out[w++] = *s++;
				}
			}
			out[w] = 0;
			return;
		}
	}
}

static void strip_html(const char *s, size_t n, Buf *out) {
	int in_tag = 0, skip = 0;
	for (size_t i = 0; i < n; i++) {
		if (s[i] == '<') {
			in_tag = 1;
			if (!strncasecmp(s + i, "<style", 6) || !strncasecmp(s + i, "<script", 7)) {
				skip = 1;
			} else if (!strncasecmp(s + i, "</style", 7) || !strncasecmp(s + i, "</script", 8)) {
				skip = 0;
			} else if (!strncasecmp(s + i, "<br", 3) || !strncasecmp(s + i, "<p", 2) ||
				!strncasecmp(s + i, "</p", 3) || !strncasecmp(s + i, "<div", 4) || !strncasecmp(s + i, "<tr", 3)) {
				buf_puts(out, "\n");
			}
			continue;
		}
		if (in_tag) {
			if (s[i] == '>') {
				in_tag = 0;
			}
			continue;
		}
		if (skip) {
			continue;
		}
		if (s[i] == '&') {
			static const struct { const char *e, *r; } ents[] = {
				{"&nbsp;", " "}, {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"},
			};
			int hit = 0;
			for (size_t k = 0; k < sizeof ents / sizeof ents[0]; k++) {
				size_t el = strlen(ents[k].e);
				if (i + el <= n && !strncasecmp(s + i, ents[k].e, el)) {
					buf_puts(out, ents[k].r);
					i += el - 1;
					hit = 1;
					break;
				}
			}
			if (hit) {
				continue;
			}
		}
		if (s[i] != '\r') {
			buf_put(out, s + i, 1);
		}
	}
}

/* the decoded body of one leaf part */
static void leaf(const char *m, size_t n, const char *ct, Buf *out) {
	Buf cte = {0}, raw = {0};
	header_raw(m, n, "Content-Transfer-Encoding", &cte);
	size_t b = body_at(m, n);
	const char *e = cte.p ? cte.p : "";
	while (isspace((unsigned char)*e)) {
		e++;
	}
	if (!strncasecmp(e, "base64", 6)) {
		b64_decode(m + b, n - b, &raw);
	} else if (!strncasecmp(e, "quoted-printable", 16)) {
		qp_decode(m + b, n - b, 0, &raw);
	} else {
		buf_put(&raw, m + b, n - b);
	}
	char cs[40];
	param(ct, "charset", cs, sizeof cs);
	Buf u = {0};
	to_utf8(cs, raw.p ? raw.p : "", raw.n, &u);
	if (!strncasecmp(ct, "text/html", 9)) {
		strip_html(u.p ? u.p : "", u.n, out);
	} else {
		for (size_t i = 0; i < u.n; i++) {
			if (u.p[i] != '\r') {
				buf_put(out, u.p + i, 1);
			}
		}
	}
	buf_free(&u);
	buf_free(&raw);
	buf_free(&cte);
}

/* finds the best text in m: want is "text/plain" first, then "text/html"; 0 when found */
static int walk(const char *m, size_t n, const char *want, int depth, Buf *out) {
	if (depth > MIME_DEPTH) {
		return -1;
	}
	Buf ct = {0};
	if (header_raw(m, n, "Content-Type", &ct) != 0) {
		buf_puts(&ct, "text/plain");
	}
	int rc = -1;
	if (!strncasecmp(ct.p, "multipart/", 10)) {
		char bd[200], delim[204];
		param(ct.p, "boundary", bd, sizeof bd);
		if (bd[0]) {
			snprintf(delim, sizeof delim, "--%s", bd);
			size_t dl = strlen(delim);
			const char *end = m + n, *p = m + body_at(m, n);
			const char *part = NULL;
			while (p < end) {
				const char *nl = memchr(p, '\n', (size_t)(end - p));
				const char *le = nl ? nl : end;
				if ((size_t)(le - p) >= dl && !strncmp(p, delim, dl)) {
					if (part && rc != 0) {
						/* the part ends at the CRLF before the delimiter line */
						const char *pe = p;
						if (pe > part && pe[-1] == '\n') pe--;
						if (pe > part && pe[-1] == '\r') pe--;
						rc = walk(part, (size_t)(pe - part), want, depth + 1, out);
					}
					if (le - p >= (long)dl + 2 && p[dl] == '-' && p[dl + 1] == '-') {
						break;
					}
					part = nl ? nl + 1 : end;
				}
				p = nl ? nl + 1 : end;
			}
		}
	} else if (!strncasecmp(ct.p, want, strlen(want))) {
		Buf cd = {0};
		header_raw(m, n, "Content-Disposition", &cd);
		if (!cd.p || strncasecmp(cd.p, "attachment", 10)) {
			leaf(m, n, ct.p, out);
			rc = 0;
		}
		buf_free(&cd);
	}
	buf_free(&ct);
	return rc;
}

void mime_text(const char *msg, size_t n, Buf *out) {
	if (walk(msg, n, "text/plain", 0, out) == 0) {
		return;
	}
	if (walk(msg, n, "text/html", 0, out) == 0) {
		return;
	}
	buf_puts(out, "(no readable text in this message)\n");
}
