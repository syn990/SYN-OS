/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
/* Lifted from SYN-CASTER's fn_gmail.c (G204): the same checks and the same plain RFC 5322 text, so the crew's mail and
 * SYN-Mail's read alike. */
#include "msg.h"

#include <ctype.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>

static int ascii_plain(const char *s) {
	for (; *s; s++) {
		if ((unsigned char)*s >= 0x7f || (unsigned char)*s < 0x20) {
			return 0;
		}
	}
	return 1;
}

/* a header word: as it is when plain ASCII, else RFC 2047 "=?UTF-8?B?...?=" */
static void header_word(Buf *out, const char *s) {
	if (ascii_plain(s)) {
		buf_puts(out, s);
		return;
	}
	buf_puts(out, "=?UTF-8?B?");
	b64_encode(s, strlen(s), out);
	buf_puts(out, "?=");
}

/* "Name" <addr>, the name quoted or encoded */
static void header_addr(Buf *out, const char *name, const char *addr) {
	if (name && *name) {
		if (ascii_plain(name)) {
			buf_puts(out, "\"");
			for (const char *p = name; *p; p++) {
				if (*p == '"' || *p == '\\') {
					buf_puts(out, "\\");
				}
				buf_put(out, p, 1);
			}
			buf_puts(out, "\"");
		} else {
			header_word(out, name);
		}
		buf_puts(out, " ");
	}
	buf_puts(out, "<");
	buf_puts(out, addr);
	buf_puts(out, ">");
}

int msg_addr_ok(const char *a) {
	if (!a || !*a || strlen(a) > 120) {
		return 0;
	}
	const char *at = strchr(a, '@');
	if (!at || at == a || !at[1] || strchr(at + 1, '@') || !strchr(at + 1, '.')) {
		return 0;
	}
	for (const char *p = a; *p; p++) {
		if (isspace((unsigned char)*p) || strchr("<>()[],;:\"\\", *p) || (unsigned char)*p < 0x20) {
			return 0;
		}
	}
	return 1;
}

void msg_id(const char *from_addr, char *out, size_t n) {
	unsigned char r[12];
	RAND_bytes(r, sizeof r);
	char hex[25];
	for (size_t i = 0; i < sizeof r; i++) {
		snprintf(hex + 2 * i, 3, "%02x", r[i]);
	}
	const char *at = strchr(from_addr, '@');
	snprintf(out, n, "<%ld.%s@%s>", (long)time(NULL), hex, at ? at + 1 : "syn-mail.invalid");
}

int msg_build(const char *from_name, const char *from_addr, const char *to, const char *subject, const char *body,
	const char *msgid, const char *reply_to, time_t when, Buf *out) {
	if (!msg_addr_ok(from_addr) || !msg_addr_ok(to) || !msgid || !*msgid) {
		return -1;
	}
	char date[64];
	struct tm tm;
	localtime_r(&when, &tm);
	strftime(date, sizeof date, "%a, %d %b %Y %H:%M:%S %z", &tm);
	char subj[240];
	snprintf(subj, sizeof subj, "%s", subject ? subject : "");
	for (char *p = subj; *p; p++) {
		if (*p == '\r' || *p == '\n') {
			*p = ' ';
		}
	}
	buf_puts(out, "From: ");
	header_addr(out, from_name, from_addr);
	buf_puts(out, "\r\nTo: ");
	header_addr(out, NULL, to);
	buf_puts(out, "\r\nSubject: ");
	header_word(out, subj);
	buf_puts(out, "\r\nDate: ");
	buf_puts(out, date);
	buf_puts(out, "\r\nMessage-ID: ");
	buf_puts(out, msgid);
	if (reply_to && *reply_to) {
		buf_puts(out, "\r\nIn-Reply-To: ");
		buf_puts(out, reply_to);
		buf_puts(out, "\r\nReferences: ");
		buf_puts(out, reply_to);
	}
	buf_puts(out, "\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
		"Content-Transfer-Encoding: base64\r\nX-Mailer: SYN-Mail\r\n\r\n");
	Buf b = {0};
	b64_encode(body ? body : "", body ? strlen(body) : 0, &b);
	for (size_t i = 0; i < b.n; i += 76) {
		buf_put(out, b.p + i, b.n - i < 76 ? b.n - i : 76);
		buf_puts(out, "\r\n");
	}
	buf_free(&b);
	return 0;
}
