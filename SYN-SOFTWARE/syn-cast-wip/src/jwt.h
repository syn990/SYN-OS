/* ------------------------------------------------------------------------
 *   The Channel API auth_token: an RS256 JWT {"iss": issuer, "exp": now+ttl}
 *   signed with the developer's private key (Friends & Family accounts).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_JWT_H
#define SYN_CAST_JWT_H

#include <stddef.h>

/* Returns a malloc'd token, or NULL with a reason in err. */
char *jwt_mint(const char *key_path, const char *issuer, int ttl_seconds, char *err, size_t errn);

#endif
