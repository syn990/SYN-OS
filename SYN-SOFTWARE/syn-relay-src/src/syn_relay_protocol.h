/* ------------------------------------------------------------------------
 *   The wire protocol shared by syn-relay's stats server role and
 *   stats client role: a fixed TCP port, newline-terminated
 *   single-word commands in, one JSON line out. No auth, no TLS in v1
 *   — deliberate, documented trade-off for trusted-LAN use only (see
 *   PROTOCOL.md).
 *
 *   Commands:
 *     STATS\n        -> {"hostname":..,"cpu_pct":..,"mem_used_kb":..,
 *                         "mem_total_kb":..,"disk_used_kb":..,
 *                         "disk_total_kb":..,"gpu_pct":..|null,
 *                         "sunshine":{"running":bool,"port":47989}}
 *     LIST_APPS\n    -> {"apps":[{"id":..,"name":..,"icon":..}, ...]}
 *     LAUNCH_APP <id>\n -> {"ok":true} | {"ok":false,"error":".."}
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (Remote node)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_RELAY_PROTOCOL_H
#define SYN_RELAY_PROTOCOL_H

/* Adjacent to Sunshine's own 47989 (HTTP) / 47990 (HTTPS) pair — avoids
 * collision, easy to remember as "next to Sunshine". */
#define SYN_RELAY_PORT 47991

#define SYN_RELAY_MAX_LINE 1024

#endif
