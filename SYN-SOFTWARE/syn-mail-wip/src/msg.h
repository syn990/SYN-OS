/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_MSG_H
#define SYN_MAIL_MSG_H
#include "letter.h"
#include "util.h"
#include <time.h>

/* an address the mail may go to: one line, local@domain, no spaces or brackets */
int msg_addr_ok(const char *a);
/* a fresh Message-ID at the sender's domain */
void msg_id(const char *from_addr, char *out, size_t n);
/* the whole message as RFC 5322 text, body base64 in lines of 76; -1 on a bad address */
int msg_build(const char *from_name, const char *from_addr, const char *to, const char *subject, const char *body,
	const char *msgid, const char *reply_to, time_t when, Buf *out);

/* one file to attach: its name as the reader sees it (no folders), its MIME type, its bytes */
typedef struct {
	const char *name;
	const char *type;
	const unsigned char *data;
	size_t n;
} MsgAttach;
/* the MIME type for a file name, by its extension; application/octet-stream when unknown */
const char *msg_type_for(const char *name);
/* as msg_build; with natt > 0 the message is multipart/mixed: the text first, then each file base64 in lines of 76,
 * its name plain and quoted when it is safe ASCII, else RFC 2231 (filename*=UTF-8''...) so no header can be forged */
int msg_build_attach(const char *from_name, const char *from_addr, const char *to, const char *subject,
	const char *body, const char *msgid, const char *reply_to, time_t when, const MsgAttach *att, int natt, Buf *out);
/* as msg_build_attach; with sign the text goes as a company letter (G265): multipart/alternative, the text twin with
 * its signature and notice, then the rendered HTML letter; inside multipart/mixed, first, when there are files.
 * sign NULL is msg_build_attach exactly */
int msg_build_letter(const char *from_name, const char *from_addr, const char *to, const char *subject,
	const char *body, const char *msgid, const char *reply_to, time_t when, const MsgAttach *att, int natt,
	const MsgSign *sign, Buf *out);
#endif
