/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "imap.h"
#include "oauth.h"
#include "secret.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void clear_resp(Imap *im) {
	for (int i = 0; i < im->nresp; i++) {
		free(im->resp[i].text);
		buf_free(&im->resp[i].lit);
	}
	free(im->resp);
	im->resp = NULL;
	im->nresp = 0;
}

/* the size of a literal announced at the end of a line ("{123}" or "{123+}"), or -1 */
static long literal_at_end(const Buf *l) {
	if (l->n < 3 || l->p[l->n - 1] != '}') {
		return -1;
	}
	char *o = strrchr(l->p, '{');
	if (!o) {
		return -1;
	}
	char *e;
	long v = strtol(o + 1, &e, 10);
	if (e == o + 1 || (*e != '}' && !(*e == '+' && e[1] == '}')) || v < 0) {
		return -1;
	}
	return v;
}

/* The largest literal, and the largest whole response, the reader will hold. A server that announces more is refused
 * before a byte of it is read, so a hostile or broken server cannot make the client allocate gigabytes. Gmail takes
 * in at most 50 MB a message, base64 included, well under the limit. */
#define IMAP_LITERAL_MAX ((long)64 << 20)

/* one whole response (a line and any literals it announces) */
static int read_resp(Imap *im, Buf *text, Buf *lit) {
	Buf line = {0};
	int first_lit = 1;
	long total = 0;
	for (;;) {
		if (net_line(im->net, &line) != 0) {
			buf_free(&line);
			return -1;
		}
		buf_put(text, line.p, line.n);
		total += (long)line.n;
		long ln = literal_at_end(&line);
		if (ln < 0) {
			break;
		}
		if (ln > IMAP_LITERAL_MAX || total + ln > IMAP_LITERAL_MAX) {
			/* the stream cannot be resynchronised without reading it, so the connection is given up */
			snprintf(im->status, sizeof im->status, "the server sent a reply too large to read (%ld bytes; the limit is %ld MiB)",
				total + ln, IMAP_LITERAL_MAX >> 20);
			buf_free(&line);
			return -2;
		}
		Buf tmp = {0};
		if (net_bytes(im->net, (size_t)ln, first_lit ? lit : &tmp) != 0) {
			buf_free(&tmp);
			buf_free(&line);
			return -1;
		}
		buf_free(&tmp);
		total += ln;
		first_lit = 0;
	}
	buf_free(&line);
	return 0;
}

static int vcmd(Imap *im, const char *cont, const char *fmt, va_list ap) {
	clear_resp(im);
	char tag[16];
	snprintf(tag, sizeof tag, "A%03d", ++im->tag);
	Buf c = {0};
	buf_printf(&c, "%s ", tag);
	va_list aq;
	va_copy(aq, ap);
	int n = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);
	char *body = malloc((size_t)n + 1);
	vsnprintf(body, (size_t)n + 1, fmt, ap);
	buf_puts(&c, body);
	free(body);
	buf_puts(&c, "\r\n");
	int w = net_puts(im->net, c.p);
	buf_wipe(&c);
	if (w != 0) {
		str_copy(im->status, sizeof im->status, "connection lost");
		return -1;
	}
	size_t tl = strlen(tag);
	for (;;) {
		Buf text = {0}, lit = {0};
		int rr = read_resp(im, &text, &lit);
		if (rr != 0) {
			if (rr == -1) {
				str_copy(im->status, sizeof im->status, "connection lost");
			}
			buf_free(&text);
			buf_free(&lit);
			return -1;
		}
		if (text.n && text.p[0] == '+') {
			/* a continuation: during AUTHENTICATE it carries the error; answer it empty to get the NO */
			net_puts(im->net, cont ? cont : "\r\n");
			buf_free(&text);
			buf_free(&lit);
			continue;
		}
		if (text.n > tl && !strncmp(text.p, tag, tl) && text.p[tl] == ' ') {
			str_copy(im->status, sizeof im->status, text.p + tl + 1);
			buf_free(&text);
			buf_free(&lit);
			if (!strncmp(im->status, "OK", 2)) return 0;
			if (!strncmp(im->status, "NO", 2)) return 1;
			return 2;
		}
		im->resp = realloc(im->resp, sizeof *im->resp * (size_t)(im->nresp + 1));
		im->resp[im->nresp].text = text.p ? text.p : strdup("");
		im->resp[im->nresp].lit = lit;
		im->nresp++;
	}
}

int imap_cmd(Imap *im, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	int rc = vcmd(im, NULL, fmt, ap);
	va_end(ap);
	return rc;
}

void imap_quote(Buf *out, const char *s) {
	buf_puts(out, "\"");
	for (; *s; s++) {
		if (*s == '"' || *s == '\\') {
			buf_puts(out, "\\");
		}
		buf_put(out, s, 1);
	}
	buf_puts(out, "\"");
}

Imap *imap_open(const Account *a, char *err, size_t errn) {
	Net *n = net_open(a->imap_host, a->imap_port, a->imap_port != 143, err, errn);
	if (!n) {
		return NULL;
	}
	Imap *im = calloc(1, sizeof *im);
	im->net = n;
	Buf g = {0};
	if (net_line(n, &g) != 0 || strncmp(g.p, "* OK", 4) != 0) {
		snprintf(err, errn, "%s: no IMAP greeting", a->imap_host);
		buf_free(&g);
		imap_close(im);
		return NULL;
	}
	buf_free(&g);
	if (a->imap_port == 143) {
		if (imap_cmd(im, "STARTTLS") != 0 || net_starttls(n, err, errn) != 0) {
			if (!*err) {
				snprintf(err, errn, "%s: STARTTLS refused", a->imap_host);
			}
			imap_close(im);
			return NULL;
		}
	}
	return im;
}

int imap_signin(Imap *im, const Account *a, char *err, size_t errn) {
	err[0] = 0;
	int rc;
	if (a->auth == AUTH_OAUTH) {
		Buf tok = {0}, ir = {0};
		if (oauth_access(a, &tok, err, errn) != 0) {
			return -1;
		}
		oauth_xoauth2(a->address, tok.p, &ir);
		buf_wipe(&tok);
		rc = imap_cmd(im, "AUTHENTICATE XOAUTH2 %s", ir.p);
		buf_wipe(&ir);
	} else {
		char *pw = secret_get(a->name, "password", err, errn);
		if (!pw) {
			if (!*err) {
				snprintf(err, errn, "no password for %s in the keyring: run syn-mail password %s", a->name, a->name);
			}
			return -1;
		}
		Buf c = {0};
		buf_puts(&c, "LOGIN ");
		imap_quote(&c, a->address);
		buf_puts(&c, " ");
		imap_quote(&c, pw);
		secret_free(pw);
		rc = imap_cmd(im, "%s", c.p);
		buf_wipe(&c);
	}
	if (rc != 0) {
		snprintf(err, errn, "%s refused the sign-in for %s: %s", a->imap_host, a->name, im->status);
		return -1;
	}
	return 0;
}

void imap_close(Imap *im) {
	if (!im) {
		return;
	}
	if (im->net) {
		net_puts(im->net, "Z999 LOGOUT\r\n");
		net_close(im->net);
	}
	clear_resp(im);
	free(im);
}
