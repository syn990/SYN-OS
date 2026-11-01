/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_MAIL_SECRET_H
#define SYN_MAIL_SECRET_H
#include <stddef.h>

/* Secrets live in the desktop's keyring (Secret Service: KWallet or GNOME Keyring), as items with the attributes
 * service=syn-mail, account=NAME, kind=KIND. Kinds: password, refresh_token, client_id, client_secret.
 * `secret-tool lookup service syn-mail account NAME kind KIND` reads the same items. */

/* a newly allocated copy, or NULL if there is none; free it with secret_free */
char *secret_get(const char *account, const char *kind, char *err, size_t errn);
int secret_set(const char *account, const char *kind, const char *value, char *err, size_t errn);
int secret_clear(const char *account, const char *kind);
void secret_free(char *s);
#endif
