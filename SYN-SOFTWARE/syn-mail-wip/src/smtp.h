/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_SMTP_H
#define SYN_MAIL_SMTP_H
#include "config.h"

/* hands one message to the account's submission server: TLS (465) or STARTTLS (587), AUTH PLAIN or XOAUTH2, one
 * recipient, the message dot-stuffed */
int smtp_send(const Account *a, const char *to, const char *message, char *err, size_t errn);
#endif
