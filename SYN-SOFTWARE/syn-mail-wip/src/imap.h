/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_IMAP_H
#define SYN_MAIL_IMAP_H
#include "config.h"
#include "net.h"

/* IMAP4rev1 (RFC 3501), the part a reader needs: sign-in (LOGIN, AUTHENTICATE XOAUTH2), EXAMINE, FETCH. */

typedef struct {
	char *text;  /* the response line(s), a literal shown as {N} */
	Buf lit;     /* the first literal it carried, e.g. the header block of a FETCH */
} ImapResp;

typedef struct {
	Net *net;
	int tag;
	ImapResp *resp; /* untagged responses of the last command */
	int nresp;
	char status[512]; /* the tagged completion line of the last command */
} Imap;

Imap *imap_open(const Account *a, char *err, size_t errn);
/* signs in with the account's keyring secret: password, or an access token from OAuth */
int imap_signin(Imap *im, const Account *a, char *err, size_t errn);
/* 0 OK, 1 NO, 2 BAD, -1 connection lost; the untagged responses in im->resp */
int imap_cmd(Imap *im, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* a string as an IMAP quoted string; -1, and nothing added, if it holds CR or LF */
int imap_quote(Buf *out, const char *s);
void imap_close(Imap *im);
#endif
