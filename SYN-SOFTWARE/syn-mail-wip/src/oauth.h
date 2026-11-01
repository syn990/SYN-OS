/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_OAUTH_H
#define SYN_MAIL_OAUTH_H
#include "config.h"
#include "util.h"

/* Google sign-in for an installed app: a loopback redirect on 127.0.0.1 with PKCE (RFC 8252, RFC 7636), scope
 * https://mail.google.com/. The client id and secret of the Desktop OAuth client are keyring items (kinds client_id,
 * client_secret) on the account; the refresh token it earns is kept there as kind refresh_token. */

/* opens the consent page (xdg-open unless open_browser is 0, the address goes to stderr either way), waits up to
 * wait_s for the redirect, trades the code and stores the refresh token */
int oauth_login(const Account *a, int open_browser, int wait_s, char *err, size_t errn);
/* a fresh access token from the stored refresh token, into tok */
int oauth_access(const Account *a, Buf *tok, char *err, size_t errn);
/* the XOAUTH2 initial response, base64: "user=ADDR^Aauth=Bearer TOKEN^A^A" */
void oauth_xoauth2(const char *user, const char *token, Buf *out);
#endif
