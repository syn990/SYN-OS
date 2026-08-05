/* ------------------------------------------------------------------------
 *   Minimal TCP client for talking to a syn-relay stats server role:
 *   connect, send one line, read one line back. No connection
 *   pooling/keepalive — every stats client subcommand is a single
 *   request-response, matching the server role's one-shot-per-
 *   connection design.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_RELAY_CONN_H
#define SYN_RELAY_CONN_H

#include <stdbool.h>
#include <stddef.h>

/* Connects to host:47991, sends `command` + "\n", reads one line of
 * reply into `reply` (up to reply_size, NUL-terminated, trailing
 * newline stripped). Returns false on any connection/IO failure —
 * host unreachable, agent not listening, timeout. Uses a short connect
 * timeout (see .c) so a dead/unreachable host fails fast rather than
 * hanging the caller. */
bool syn_relay_request(const char *host, const char *command, char *reply, size_t reply_size);

#endif
