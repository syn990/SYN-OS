/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
/* Lifted from SYN-CASTER's fn_gmail.c (G204): the same checks and the same plain RFC 5322 text, so the crew's mail and
 * SYN-Mail's read alike. */
#include "msg.h"
#include "letter.h"

#include <ctype.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

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

/* base64 of n bytes in lines of 76, each ended CRLF */
static void b64_lines(Buf *out, const void *p, size_t n) {
	Buf b = {0};
	b64_encode(p, n, &b);
	for (size_t i = 0; i < b.n; i += 76) {
		buf_put(out, b.p + i, b.n - i < 76 ? b.n - i : 76);
		buf_puts(out, "\r\n");
	}
	buf_free(&b);
}

const char *msg_type_for(const char *name) {
	static const char *map[][2] = {
		{".pdf", "application/pdf"}, {".txt", "text/plain"}, {".md", "text/markdown"}, {".csv", "text/csv"},
		{".html", "text/html"}, {".htm", "text/html"}, {".json", "application/json"}, {".xml", "application/xml"},
		{".png", "image/png"}, {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".gif", "image/gif"},
		{".webp", "image/webp"}, {".svg", "image/svg+xml"}, {".zip", "application/zip"}, {".gz", "application/gzip"},
		{".ogg", "audio/ogg"}, {".opus", "audio/ogg"}, {".mp3", "audio/mpeg"}, {".wav", "audio/wav"},
		{".mp4", "video/mp4"}, {".ics", "text/calendar"},
		{".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
		{".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
	};
	const char *dot = name ? strrchr(name, '.') : NULL;
	if (dot) {
		for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
			if (!strcasecmp(dot, map[i][0])) {
				return map[i][1];
			}
		}
	}
	return "application/octet-stream";
}

/* a file name safe to quote as it is: printable ASCII with no quote, backslash, slash or semicolon */
static int name_plain(const char *s) {
	if (!*s) {
		return 0;
	}
	for (; *s; s++) {
		if ((unsigned char)*s >= 0x7f || (unsigned char)*s < 0x20 || strchr("\"\\/;", *s)) {
			return 0;
		}
	}
	return 1;
}

/* RFC 2231 / 5987 value: UTF-8'' then every byte outside attr-char as %XX */
static void name_2231(Buf *out, const char *s) {
	buf_puts(out, "UTF-8''");
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
		if (isalnum(*p) || strchr("!#$&+-.^_`|~", *p)) {
			buf_put(out, p, 1);
		} else {
			buf_printf(out, "%%%02X", *p);
		}
	}
}

int msg_build(const char *from_name, const char *from_addr, const char *to, const char *subject, const char *body,
	const char *msgid, const char *reply_to, time_t when, Buf *out) {
	return msg_build_attach(from_name, from_addr, to, subject, body, msgid, reply_to, when, NULL, 0, out);
}

int msg_build_attach(const char *from_name, const char *from_addr, const char *to, const char *subject,
	const char *body, const char *msgid, const char *reply_to, time_t when, const MsgAttach *att, int natt, Buf *out) {
	return msg_build_letter(from_name, from_addr, to, subject, body, msgid, reply_to, when, att, natt, NULL, out);
}

/* a fresh boundary: '=' and '_' never both appear in base64 lines or the plain headers written here */
static void boundary(char *b, size_t n, const char *tag) {
	unsigned char r[12];
	RAND_bytes(r, sizeof r);
	snprintf(b, n, "=_SYN-Mail_%s_", tag);
	for (size_t i = 0; i < sizeof r; i++) {
		snprintf(b + strlen(b), 3, "%02x", r[i]);
	}
}

/* one text part, UTF-8 in base64 */
static void text_part(Buf *out, const char *bound, const char *type, const char *text, size_t n) {
	buf_printf(out, "--%s\r\nContent-Type: %s; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n", bound, type);
	b64_lines(out, text, n);
}

int msg_build_letter(const char *from_name, const char *from_addr, const char *to, const char *subject,
	const char *body, const char *msgid, const char *reply_to, time_t when, const MsgAttach *att, int natt,
	const MsgSign *sign, Buf *out) {
	if (!msg_addr_ok(from_addr) || !msg_addr_ok(to) || !msgid || !*msgid || natt < 0 || (natt > 0 && !att)) {
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
	const char *text = body ? body : "";
	if (natt == 0 && !sign) {
		buf_puts(out, "\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\n"
			"Content-Transfer-Encoding: base64\r\nX-Mailer: SYN-Mail\r\n\r\n");
		b64_lines(out, text, strlen(text));
		return 0;
	}
	char mix[64] = "", alt[64] = "";
	if (natt) boundary(mix, sizeof mix, "mix");
	if (sign) boundary(alt, sizeof alt, "alt");
	buf_printf(out, "\r\nMIME-Version: 1.0\r\nContent-Type: multipart/%s; boundary=\"%s\"\r\n"
		"X-Mailer: SYN-Mail\r\n\r\nThis is a multi-part message in MIME format.\r\n",
		natt ? "mixed" : "alternative", natt ? mix : alt);
	if (sign) {
		/* the letter: its text twin first, the rendered HTML last (the one a client shows when it can) */
		if (natt) buf_printf(out, "--%s\r\nContent-Type: multipart/alternative; boundary=\"%s\"\r\n\r\n", mix, alt);
		Buf t = {0}, h = {0};
		letter_text(text, sign, &t);
		letter_html(subj, text, sign, &h);
		text_part(out, alt, "text/plain", t.p ? t.p : "", t.n);
		text_part(out, alt, "text/html", h.p ? h.p : "", h.n);
		buf_free(&t);
		buf_free(&h);
		buf_printf(out, "--%s--\r\n", alt);
	} else {
		text_part(out, mix, "text/plain", text, strlen(text));
	}
	for (int i = 0; i < natt; i++) {
		const char *name = att[i].name && *att[i].name ? att[i].name : "attachment";
		const char *type = att[i].type && *att[i].type ? att[i].type : msg_type_for(name);
		for (const char *p = type; *p; p++) {
			if (!isalnum((unsigned char)*p) && !strchr("/.+-", *p)) {
				type = "application/octet-stream";
				break;
			}
		}
		buf_printf(out, "--%s\r\nContent-Type: %s", mix, type);
		if (name_plain(name)) {
			buf_printf(out, "; name=\"%s\"\r\nContent-Disposition: attachment; filename=\"%s\"\r\n", name, name);
		} else {
			/* always encoded here: a plain quote or backslash in the name must not end the quoted string */
			buf_puts(out, "; name=\"=?UTF-8?B?");
			b64_encode(name, strlen(name), out);
			buf_puts(out, "?=\"\r\nContent-Disposition: attachment; filename*=");
			name_2231(out, name);
			buf_puts(out, "\r\n");
		}
		buf_puts(out, "Content-Transfer-Encoding: base64\r\n\r\n");
		b64_lines(out, att[i].data, att[i].n);
	}
	if (natt) buf_printf(out, "--%s--\r\n", mix);
	return 0;
}
