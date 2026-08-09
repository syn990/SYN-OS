/* ------------------------------------------------------------------------
 *   Resolves ~/.ssh/config Host aliases (HostName, ProxyJump, Include,
 *   wildcard Match blocks, etc.) by asking the real `ssh` client to do
 *   it, instead of re-implementing ssh_config's grammar here. See the
 *   .c file for why.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SSH_RESOLVE_H
#define SYN_SSH_RESOLVE_H

#include <stdbool.h>
#include <stddef.h>

/* Resolves `host_alias` (as typed, or picked from ~/.ssh/config) to the
 * HostName ssh_config would actually connect to, via `ssh -G`. Does NOT
 * open any connection — pure config evaluation. If `host_alias` has no
 * matching ssh_config entry, ssh_config's own default applies: the
 * alias itself is echoed back unchanged, so this is always safe to call
 * even for a bare IP or a hostname with no ~/.ssh/config entry at all.
 * Returns false only if `ssh` itself can't be run or produced no
 * parseable "hostname" line. */
bool syn_ssh_resolve_hostname(const char *host_alias, char *resolved_out, size_t resolved_out_size);

#endif
