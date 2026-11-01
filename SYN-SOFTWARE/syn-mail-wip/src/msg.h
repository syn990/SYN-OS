/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_MSG_H
#define SYN_MAIL_MSG_H
#include "util.h"
#include <time.h>

/* an address the mail may go to: one line, local@domain, no spaces or brackets */
int msg_addr_ok(const char *a);
/* a fresh Message-ID at the sender's domain */
void msg_id(const char *from_addr, char *out, size_t n);
/* the whole message as RFC 5322 text, body base64 in lines of 76; -1 on a bad address */
int msg_build(const char *from_name, const char *from_addr, const char *to, const char *subject, const char *body,
	const char *msgid, const char *reply_to, time_t when, Buf *out);
#endif
