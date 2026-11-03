/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
/* G265: the company letter. Every mail syn-mail sends goes as a text twin and a rendered HTML letter in the pinned
 * SYN-OS-M141 look, with a full signature block and the Syntax Corporation footer. HTML mail is read by clients that
 * strip <style>, block images and rewrite links, so this keeps to what all of them show alike: nested tables, inline
 * styles, web-safe font fallbacks, colours as plain hex, no images (remote or inline), no scripts, no links. */
#include "letter.h"

#include <string.h>

const char LETTER_COMPANY[] = "Syntax Corporation";

const char LETTER_NOTICE[] =
	"NOTICE OF GENERATED WORKS AND DISCLAIMER OF WARRANTY. This message and any document attached to it are works "
	"generated for Syntax Corporation by its crew of automated agents, a variety of polymorphic code that rewrites "
	"itself and may behave differently from one run to the next. Such works are experimental, unstable and "
	"unreliable, and the agents that produce them are likely to hallucinate: they may state facts, figures, "
	"sources, test results or events that are inaccurate, incomplete or entirely invented. Nothing herein is fit "
	"for, or may be relied upon for, the design, production, handling or use of nuclear material, explosives, "
	"weapons or controlled drugs, nor for any medical, legal, financial or safety-critical decision. All works are "
	"provided \"as is\" and \"as available\", without warranty of any kind, express or implied, including but not "
	"limited to warranties of merchantability, fitness for a particular purpose, accuracy and non-infringement. To "
	"the fullest extent permitted by law, Syntax Corporation, its owner and its crew accept no liability for any "
	"loss or damage arising from their use. Verify independently before acting. Copyright 2026 Syntax "
	"Corporation. All rights reserved.";

const char LETTER_THEME[] = "SYN-OS-M141";

/* SYN-OS-M141 (~/.config/syn-os/themes/SYN-OS-M141.theme), pinned (the boss, 3 Nov 03:08: "SYN-OS-M141 pls not blue"),
 * the same colours as SYN-CASTER's crew letter (src/fn_gmail.c): change both together. C_MUTED is not in the theme: a
 * neutral grey between its text and its border, for the address and the notice */
#define C_BG "#000000"
#define C_BG_ALT "#0a0a0a"
#define C_PANEL "#1a0000"
#define C_ACCENT "#e00000"
#define C_ACCENT_DIM "#4d0000"
#define C_TEXT "#ffffff"
#define C_MUTED "#b3b3b3"
#define C_BORDER "#555555"
/* the desktop's text font (Roboto) with the fallbacks every client has; the mark in a monospace every client has
 * (Terminus is a bitmap font: Chrome draws nothing at sizes it lacks, so it is left out) */
#define F_TEXT "Roboto,'Segoe UI',Helvetica,Arial,sans-serif"
#define F_MARK "'Lucida Console','Courier New',Courier,monospace"

static int has(const char *s) { return s && *s; }

/* text as HTML: & < > " ' escaped, CR and other control bytes (bar tab) dropped, newlines as given to nl */
static void esc(Buf *out, const char *s, size_t n, const char *nl) {
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '&': buf_puts(out, "&amp;"); break;
		case '<': buf_puts(out, "&lt;"); break;
		case '>': buf_puts(out, "&gt;"); break;
		case '"': buf_puts(out, "&quot;"); break;
		case '\'': buf_puts(out, "&#39;"); break;
		case '\n': buf_puts(out, nl); break;
		case '\t': buf_puts(out, " "); break;
		default:
			if (c >= 0x20 && c != 0x7f) buf_put(out, &s[i], 1);
		}
	}
}

static void esc_s(Buf *out, const char *s) { esc(out, s, strlen(s), " "); }

/* the body with CR dropped and trailing blank lines cut */
static void clean_body(const char *body, Buf *out) {
	for (const char *p = body ? body : ""; *p; p++)
		if (*p != '\r') buf_put(out, p, 1);
	while (out->n && (out->p[out->n - 1] == '\n' || out->p[out->n - 1] == ' ')) out->p[--out->n] = 0;
}

void letter_text(const char *body, const MsgSign *s, Buf *out) {
	Buf b = {0};
	clean_body(body, &b);
	if (b.n) buf_put(out, b.p, b.n);
	buf_free(&b);
	buf_puts(out, "\n\n-- \n");
	if (s && has(s->name)) buf_printf(out, "%s\n", s->name);
	if (s && has(s->title)) buf_printf(out, "%s\n", s->title);
	buf_printf(out, "%s\n", s && has(s->company) ? s->company : LETTER_COMPANY);
	if (s && has(s->address)) buf_printf(out, "%s\n", s->address);
	buf_printf(out, "\n%s\n", LETTER_NOTICE);
}

/* the body as paragraphs: a run of blank lines starts a new one, a single newline is a <br> */
static void body_html(Buf *out, const char *body) {
	Buf b = {0};
	clean_body(body, &b);
	const char *p = b.p ? b.p : "";
	while (*p == '\n') p++;
	while (*p) {
		const char *end = strstr(p, "\n\n");
		size_t n = end ? (size_t)(end - p) : strlen(p);
		buf_puts(out, "<p style=\"margin:0 0 16px 0;font-family:" F_TEXT ";font-size:15px;line-height:23px;color:"
			C_TEXT ";\">");
		esc(out, p, n, "<br>\r\n");
		buf_puts(out, "</p>\r\n");
		p += n;
		while (*p == '\n') p++;
	}
	buf_free(&b);
}

void letter_html(const char *subject, const char *body, const MsgSign *s, Buf *out) {
	const char *company = s && has(s->company) ? s->company : LETTER_COMPANY;
	buf_puts(out, "<!DOCTYPE html>\r\n<html lang=\"en\">\r\n<head>\r\n"
		"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=UTF-8\">\r\n"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\r\n"
		"<meta name=\"color-scheme\" content=\"dark\">\r\n<title>");
	esc_s(out, subject ? subject : "");
	buf_puts(out, "</title>\r\n</head>\r\n"
		"<body style=\"margin:0;padding:0;background-color:" C_BG_ALT ";\">\r\n"
		"<table role=\"presentation\" width=\"100%\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" bgcolor=\""
		C_BG_ALT "\" style=\"background-color:" C_BG_ALT ";\">\r\n<tr><td align=\"center\" style=\"padding:24px 12px;\">\r\n"
		"<table role=\"presentation\" width=\"600\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\" bgcolor=\""
		C_PANEL "\" style=\"width:600px;max-width:100%;background-color:" C_PANEL ";border:1px solid " C_ACCENT_DIM ";\">\r\n");
	/* the mark: text, not an image, so it shows with images blocked */
	buf_puts(out, "<tr><td style=\"padding:18px 32px;border-bottom:3px solid " C_ACCENT ";background-color:" C_BG ";\">"
		"<span style=\"font-family:" F_MARK ";font-size:20px;font-weight:bold;letter-spacing:2px;color:" C_ACCENT
		";\">SYN-OS</span><span style=\"font-family:" F_MARK ";font-size:13px;letter-spacing:3px;color:" C_TEXT
		";\">&nbsp;&nbsp;|&nbsp;&nbsp;");
	esc_s(out, company);
	buf_puts(out, "</span></td></tr>\r\n");
	/* the letter */
	buf_puts(out, "<tr><td style=\"padding:28px 32px 8px 32px;\">\r\n");
	body_html(out, body);
	buf_puts(out, "</td></tr>\r\n");
	/* the signature block */
	buf_puts(out, "<tr><td style=\"padding:8px 32px 28px 32px;\">\r\n"
		"<table role=\"presentation\" cellpadding=\"0\" cellspacing=\"0\" border=\"0\"><tr>"
		"<td style=\"border-left:3px solid " C_ACCENT ";padding:2px 0 2px 14px;font-family:" F_TEXT ";\">\r\n");
	if (s && has(s->name)) {
		buf_puts(out, "<div style=\"font-size:17px;line-height:22px;font-weight:bold;color:" C_TEXT ";\">");
		esc_s(out, s->name);
		buf_puts(out, "</div>\r\n");
	}
	if (s && has(s->title)) {
		buf_puts(out, "<div style=\"font-size:13px;line-height:19px;color:" C_ACCENT ";\">");
		esc_s(out, s->title);
		buf_puts(out, "</div>\r\n");
	}
	buf_puts(out, "<div style=\"font-family:" F_MARK ";font-size:12px;line-height:19px;letter-spacing:2px;color:"
		C_TEXT ";text-transform:uppercase;\">");
	esc_s(out, company);
	buf_puts(out, "</div>\r\n");
	if (s && has(s->address)) {
		/* plain text, not a mailto link: some clients turn it into one themselves */
		buf_puts(out, "<div style=\"font-size:12px;line-height:18px;color:" C_MUTED ";\">");
		esc_s(out, s->address);
		buf_puts(out, "</div>\r\n");
	}
	buf_puts(out, "</td></tr></table>\r\n</td></tr>\r\n");
	/* the company footer */
	buf_puts(out, "<tr><td style=\"padding:16px 32px 20px 32px;background-color:" C_BG ";border-top:1px solid " C_BORDER
		";\">\r\n<p style=\"margin:0 0 8px 0;font-family:" F_MARK ";font-size:11px;letter-spacing:2px;color:" C_ACCENT
		";\">SYN-OS&nbsp;&nbsp;|&nbsp;&nbsp;");
	esc_s(out, company);
	buf_puts(out, "</p>\r\n<p style=\"margin:0;font-family:" F_TEXT ";font-size:10px;line-height:15px;color:" C_MUTED
		";\">");
	esc_s(out, LETTER_NOTICE);
	buf_puts(out, "</p>\r\n</td></tr>\r\n</table>\r\n</td></tr>\r\n</table>\r\n</body>\r\n</html>\r\n");
}
