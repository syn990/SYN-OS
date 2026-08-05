/* ------------------------------------------------------------------------
 *   Detects whether Sunshine (the game-stream host) is running on this
 *   machine, so the stats client role knows whether a "launch stream"
 *   action is possible for this node. The stats server role never
 *   speaks Sunshine's own NVHTTP/pairing protocol itself — moonlight-qt
 *   owns that entirely.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats server role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SUNSHINE_H
#define SYN_SUNSHINE_H

#include <stdbool.h>

/* Sunshine's own default HTTP port (unencrypted, used here only for a
 * liveness probe — no data is read from it). */
#define SYN_SUNSHINE_PORT 47989

/* True only if a process literally named "sunshine" is running AND a
 * TCP connect to 127.0.0.1:SYN_SUNSHINE_PORT succeeds — matches it
 * being actually up and listening, not just installed. */
bool syn_sunshine_is_running(void);

#endif
