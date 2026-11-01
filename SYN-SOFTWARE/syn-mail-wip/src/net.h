/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_NET_H
#define SYN_MAIL_NET_H
#include "util.h"

/* one line-oriented TLS (or not yet TLS) connection to a mail server */
typedef struct Net Net;

/* tls: 1 for TLS from the first byte (993, 465), 0 for plain until net_starttls (587) */
Net *net_open(const char *host, int port, int tls, char *err, size_t errn);
int net_starttls(Net *c, char *err, size_t errn);
int net_write(Net *c, const void *p, size_t n);
int net_puts(Net *c, const char *s);
/* one line without its CRLF into out (cleared first); -1 on close or error */
int net_line(Net *c, Buf *out);
/* exactly n bytes, appended to out */
int net_bytes(Net *c, size_t n, Buf *out);
void net_close(Net *c);
/* set to print the conversation (never AUTH lines) on stderr */
extern int net_trace;
#endif
