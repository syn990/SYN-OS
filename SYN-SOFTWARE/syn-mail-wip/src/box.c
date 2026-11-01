/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "box.h"
#include "mime.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

Imap *box_connect(const Account *a, char *err, size_t errn) {
	err[0] = 0;
	Imap *im = imap_open(a, err, errn);
	if (!im) {
		return NULL;
	}
	if (imap_signin(im, a, err, errn) != 0) {
		imap_close(im);
		return NULL;
	}
	return im;
}

void box_one_line(Buf *b, size_t max) {
	if (!b->p) {
		return;
	}
	for (size_t i = 0; i < b->n; i++) {
		if (b->p[i] == '\r' || b->p[i] == '\n' || b->p[i] == '\t') b->p[i] = ' ';
	}
	if (b->n > max) {
		size_t k = max;
		while (k > 0 && ((unsigned char)b->p[k] & 0xc0) == 0x80) k--;
		b->n = k;
		b->p[k] = 0;
	}
}

static int b64v(int c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == ',') return 63;
	return -1;
}

void box_mutf7(const char *in, char *out, size_t n) {
	Buf b = {0};
	for (const char *s = in; *s;) {
		if (*s != '&') {
			buf_put(&b, s++, 1);
			continue;
		}
		s++;
		if (*s == '-') {
			buf_puts(&b, "&");
			s++;
			continue;
		}
		/* base64 of UTF-16BE, up to '-' */
		unsigned bits = 0, nb = 0;
		uint32_t hi = 0;
		while (*s && *s != '-') {
			int v = b64v((unsigned char)*s++);
			if (v < 0) break;
			bits = (bits << 6) | (unsigned)v;
			nb += 6;
			if (nb >= 16) {
				nb -= 16;
				uint32_t u = (bits >> nb) & 0xffff;
				if (u >= 0xd800 && u < 0xdc00) {
					hi = u;
				} else if (u >= 0xdc00 && u < 0xe000 && hi) {
					utf8_put(&b, 0x10000 + ((hi - 0xd800) << 10) + (u - 0xdc00));
					hi = 0;
				} else {
					utf8_put(&b, u);
					hi = 0;
				}
			}
		}
		if (*s == '-') s++;
	}
	str_copy(out, n, b.p ? b.p : "");
	buf_free(&b);
}

/* an IMAP string at *p (quoted, atom or NIL) into out; advances *p */
static void take_string(const char **p, char *out, size_t n) {
	const char *s = *p;
	while (*s == ' ') s++;
	size_t k = 0;
	if (*s == '"') {
		s++;
		while (*s && *s != '"') {
			if (*s == '\\' && s[1]) s++;
			if (k + 1 < n) out[k++] = *s;
			s++;
		}
		if (*s == '"') s++;
	} else {
		while (*s && *s != ' ') {
			if (k + 1 < n) out[k++] = *s;
			s++;
		}
	}
	out[k] = 0;
	*p = s;
}

static int folder_cmp(const void *a, const void *b) {
	const Folder *x = a, *y = b;
	int xi = !strcasecmp(x->raw, "INBOX"), yi = !strcasecmp(y->raw, "INBOX");
	if (xi != yi) return yi - xi;
	return strcasecmp(x->name, y->name);
}

int box_folders(Imap *im, Folder **out, int *n, char *err, size_t errn) {
	*out = NULL;
	*n = 0;
	if (imap_cmd(im, "LIST \"\" \"*\"") != 0) {
		snprintf(err, errn, "LIST: %s", im->status);
		return -1;
	}
	Folder *f = NULL;
	int k = 0;
	for (int i = 0; i < im->nresp; i++) {
		const char *t = im->resp[i].text;
		if (strncmp(t, "* LIST (", 8) != 0) continue;
		const char *close = strchr(t + 8, ')');
		if (!close) continue;
		Folder x = {0};
		/* flags */
		size_t fl = (size_t)(close - (t + 8));
		char flags[256];
		str_copy(flags, fl + 1 < sizeof flags ? fl + 1 : sizeof flags, t + 8);
		x.noselect = strcasestr(flags, "\\Noselect") || strcasestr(flags, "\\NonExistent");
		const char *p = close + 1;
		char delim[8];
		take_string(&p, delim, sizeof delim);
		while (*p == ' ') p++;
		if (im->resp[i].lit.n && *p == '{') {
			str_copy(x.raw, sizeof x.raw, im->resp[i].lit.p);
		} else {
			take_string(&p, x.raw, sizeof x.raw);
		}
		if (!x.raw[0]) continue;
		box_mutf7(x.raw, x.name, sizeof x.name);
		if (!strcasecmp(x.raw, "INBOX")) str_copy(x.name, sizeof x.name, "Inbox");
		f = realloc(f, sizeof *f * (size_t)(k + 1));
		f[k++] = x;
	}
	qsort(f, (size_t)k, sizeof *f, folder_cmp);
	*out = f;
	*n = k;
	return 0;
}

static long count_of(Imap *im, const char *what) {
	for (int i = 0; i < im->nresp; i++) {
		long v;
		char w[16];
		if (sscanf(im->resp[i].text, "* %ld %15s", &v, w) == 2 && !strcmp(w, what)) return v;
	}
	return -1;
}

static int examine(Imap *im, const char *raw, char *err, size_t errn) {
	Buf c = {0};
	buf_puts(&c, "EXAMINE ");
	imap_quote(&c, raw);
	int rc = imap_cmd(im, "%s", c.p);
	buf_free(&c);
	if (rc != 0) {
		snprintf(err, errn, "EXAMINE %s: %s", raw, im->status);
		return -1;
	}
	return 0;
}

time_t box_date(const char *s) {
	if (!s || !*s) return 0;
	const char *p = s;
	while (*p == ' ') p++;
	/* skip a weekday */
	const char *c = strchr(p, ',');
	if (c && c - p <= 4) p = c + 1;
	static const char *fmts[] = {" %d %b %Y %H:%M:%S %z", " %d %b %Y %H:%M %z", " %d %b %Y %H:%M:%S"};
	for (int i = 0; i < 3; i++) {
		struct tm tm = {0};
		char *e = strptime(p, fmts[i], &tm);
		if (!e) continue;
		long off = i < 2 ? tm.tm_gmtoff : 0;
		tm.tm_isdst = 0;
		return timegm(&tm) - off;
	}
	return 0;
}

void box_short_date(time_t t, time_t now, char *out, size_t n) {
	if (!t) {
		str_copy(out, n, "");
		return;
	}
	struct tm a, b;
	localtime_r(&t, &a);
	localtime_r(&now, &b);
	if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) strftime(out, n, "%H:%M", &a);
	else if (a.tm_year == b.tm_year) strftime(out, n, "%d %b", &a);
	else strftime(out, n, "%d %b %Y", &a);
}

/* "Name <addr>" into name and addr */
static void split_from(const char *f, char *name, size_t nn, char *addr, size_t an) {
	const char *lt = strrchr(f, '<'), *gt = lt ? strchr(lt, '>') : NULL;
	if (lt && gt) {
		size_t k = (size_t)(gt - lt - 1);
		str_copy(addr, k + 1 < an ? k + 1 : an, lt + 1);
		size_t m = (size_t)(lt - f);
		while (m && f[m - 1] == ' ') m--;
		size_t s = 0;
		while (s < m && (f[s] == ' ' || f[s] == '"')) s++;
		while (m > s && f[m - 1] == '"') m--;
		if (m > s) str_copy(name, m - s + 1 < nn ? m - s + 1 : nn, f + s);
		else str_copy(name, nn, addr);
	} else {
		str_copy(addr, an, f);
		str_copy(name, nn, f);
	}
}

void page_free(Page *p) {
	free(p->h);
	p->h = NULL;
	p->n = 0;
}

static int by_seq_desc(const void *a, const void *b) {
	const Header *x = a, *y = b;
	return (y->seq > x->seq) - (y->seq < x->seq);
}

int box_page(Imap *im, const char *raw, long skip, int count, Page *out, char *err, size_t errn) {
	memset(out, 0, sizeof *out);
	out->unseen = -1;
	if (examine(im, raw, err, errn) != 0) return -1;
	out->exists = count_of(im, "EXISTS");
	{
		Buf c = {0};
		buf_puts(&c, "STATUS ");
		imap_quote(&c, raw);
		buf_puts(&c, " (UNSEEN)");
		if (imap_cmd(im, "%s", c.p) == 0) {
			for (int i = 0; i < im->nresp; i++) {
				char *u = strstr(im->resp[i].text, "UNSEEN ");
				if (u) out->unseen = strtol(u + 7, NULL, 10);
			}
		}
		buf_free(&c);
	}
	if (out->exists <= 0 || count <= 0 || skip >= out->exists) return 0;
	long hi = out->exists - skip, lo = hi - count + 1 > 1 ? hi - count + 1 : 1;
	if (imap_cmd(im, "FETCH %ld:%ld (UID FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE MESSAGE-ID)])", lo, hi) != 0) {
		snprintf(err, errn, "FETCH: %s", im->status);
		return -1;
	}
	out->h = calloc((size_t)(hi - lo + 1), sizeof *out->h);
	time_t now = time(NULL);
	for (int i = 0; i < im->nresp && out->n < hi - lo + 1; i++) {
		ImapResp *r = &im->resp[i];
		long seq;
		char kind[8];
		if (sscanf(r->text, "* %ld %7s", &seq, kind) != 2 || strcmp(kind, "FETCH") || !r->lit.n) continue;
		Header *h = &out->h[out->n++];
		h->seq = seq;
		const char *u = strstr(r->text, "UID ");
		h->uid = u ? strtoul(u + 4, NULL, 10) : 0;
		h->seen = strstr(r->text, "\\Seen") != NULL;
		Buf f = {0}, s = {0}, d = {0}, m = {0};
		mime_header(r->lit.p, r->lit.n, "From", &f);
		mime_header(r->lit.p, r->lit.n, "Subject", &s);
		mime_header(r->lit.p, r->lit.n, "Date", &d);
		mime_header(r->lit.p, r->lit.n, "Message-ID", &m);
		box_one_line(&f, 300);
		box_one_line(&s, sizeof h->subject - 1);
		box_one_line(&m, sizeof h->msgid - 1);
		split_from(f.p ? f.p : "", h->from, sizeof h->from, h->from_addr, sizeof h->from_addr);
		str_copy(h->subject, sizeof h->subject, s.p ? s.p : "");
		str_copy(h->msgid, sizeof h->msgid, m.p ? m.p : "");
		h->when = box_date(d.p);
		box_short_date(h->when, now, h->date, sizeof h->date);
		buf_free(&f);
		buf_free(&s);
		buf_free(&d);
		buf_free(&m);
	}
	qsort(out->h, (size_t)out->n, sizeof *out->h, by_seq_desc);
	return 0;
}

int box_body(Imap *im, const char *raw, unsigned long uid, Buf *out, char *err, size_t errn) {
	if (examine(im, raw, err, errn) != 0) return -1;
	if (imap_cmd(im, "UID FETCH %lu BODY.PEEK[]", uid) != 0) {
		snprintf(err, errn, "UID FETCH: %s", im->status);
		return -1;
	}
	for (int i = 0; i < im->nresp; i++) {
		if (im->resp[i].lit.n) {
			buf_put(out, im->resp[i].lit.p, im->resp[i].lit.n);
			return 0;
		}
	}
	snprintf(err, errn, "no message with UID %lu in %s", uid, raw);
	return -1;
}
