/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "smtp.h"
#include "net.h"
#include "oauth.h"
#include "secret.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* a whole reply (multi-line "250-..." until "250 ..."); its code, or -1 */
static int reply(Net *n, Buf *last) {
	for (;;) {
		if (net_line(n, last) != 0 || last->n < 3) {
			return -1;
		}
		if (last->n == 3 || last->p[3] == ' ') {
			return atoi(last->p);
		}
	}
}

static int say(Net *n, Buf *last, const char *line) {
	if (net_puts(n, line) != 0) {
		return -1;
	}
	return reply(n, last);
}

int smtp_send(const Account *a, const char *to, const char *message, char *err, size_t errn) {
	err[0] = 0;
	int tls = a->smtp_port != 587 && a->smtp_port != 25;
	Net *n = net_open(a->smtp_host, a->smtp_port, tls, err, errn);
	if (!n) {
		return -1;
	}
	Buf last = {0}, line = {0};
	int rc = -1;
	const char *stage = "greeting";
	if (reply(n, &last) != 220) goto out;
	stage = "EHLO";
	if (say(n, &last, "EHLO syn-mail.localdomain\r\n") != 250) goto out;
	if (!tls) {
		stage = "STARTTLS";
		if (say(n, &last, "STARTTLS\r\n") != 220 || net_starttls(n, err, errn) != 0) goto out;
		stage = "EHLO";
		if (say(n, &last, "EHLO syn-mail.localdomain\r\n") != 250) goto out;
	}
	stage = "sign-in";
	if (a->auth == AUTH_OAUTH) {
		Buf tok = {0}, ir = {0};
		if (oauth_access(a, &tok, err, errn) != 0) goto out;
		oauth_xoauth2(a->address, tok.p, &ir);
		buf_wipe(&tok);
		buf_printf(&line, "AUTH XOAUTH2 %s\r\n", ir.p);
		buf_wipe(&ir);
		int c = say(n, &last, line.p);
		buf_wipe(&line);
		if (c == 334) {
			c = say(n, &last, "\r\n"); /* the error is in the 334; an empty answer gets the final code */
		}
		if (c != 235) goto out;
	} else {
		char *pw = secret_get(a->name, "password", err, errn);
		if (!pw) {
			if (!*err) {
				snprintf(err, errn, "no password for %s in the keyring", a->name);
			}
			goto out;
		}
		Buf raw = {0}, ir = {0};
		buf_put(&raw, "", 1);
		buf_puts(&raw, a->address);
		buf_put(&raw, "", 1);
		buf_puts(&raw, pw);
		secret_free(pw);
		b64_encode(raw.p, raw.n, &ir);
		buf_wipe(&raw);
		buf_printf(&line, "AUTH PLAIN %s\r\n", ir.p);
		buf_wipe(&ir);
		int c = say(n, &last, line.p);
		buf_wipe(&line);
		if (c != 235) goto out;
	}
	stage = "MAIL FROM";
	buf_printf(&line, "MAIL FROM:<%s>\r\n", a->address);
	int c = say(n, &last, line.p);
	buf_free(&line);
	if (c != 250) goto out;
	stage = "RCPT TO";
	buf_printf(&line, "RCPT TO:<%s>\r\n", to);
	c = say(n, &last, line.p);
	buf_free(&line);
	if (c != 250 && c != 251) goto out;
	stage = "DATA";
	if (say(n, &last, "DATA\r\n") != 354) goto out;
	/* dot-stuffing (RFC 5321 4.5.2), and bare LF made CRLF */
	Buf d = {0};
	int bol = 1;
	for (const char *p = message; *p; p++) {
		if (bol && *p == '.') {
			buf_puts(&d, ".");
		}
		if (*p == '\n' && (p == message || p[-1] != '\r')) {
			buf_puts(&d, "\r");
		}
		buf_put(&d, p, 1);
		bol = *p == '\n';
	}
	if (!bol) {
		buf_puts(&d, "\r\n");
	}
	buf_puts(&d, ".\r\n");
	int w = net_write(n, d.p, d.n);
	buf_free(&d);
	stage = "message";
	if (w != 0 || reply(n, &last) != 250) goto out;
	rc = 0;
	say(n, &last, "QUIT\r\n");
out:
	if (rc != 0 && !*err) {
		snprintf(err, errn, "%s at %s: %s", a->smtp_host, stage, last.p ? last.p : "connection lost");
	}
	buf_free(&last);
	net_close(n);
	return rc;
}
