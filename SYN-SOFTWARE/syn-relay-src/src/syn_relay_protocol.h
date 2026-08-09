/* ------------------------------------------------------------------------
 *   The wire protocol shared by syn-relay's stats server role and
 *   stats client role: a fixed TCP port, newline-terminated
 *   single-word commands in, one JSON line out. No auth, no TLS in v1
 *   — deliberate, documented trade-off for trusted-LAN use only (see
 *   PROTOCOL.md).
 *
 *   Commands:
 *     STATS\n        -> {"hostname":..,"os":"linux"|"windows"|"macos",
 *                         "capabilities":{"apps":bool,"watch_desktop":bool,
 *                         "watch_window":bool,"host_watched":bool},
 *                         "cpu_pct":..,"mem_used_kb":..,
 *                         "mem_total_kb":..,"disk_used_kb":..,
 *                         "disk_total_kb":..,"gpu_pct":..|null,
 *                         "sunshine":{"running":bool,"port":47989}}
 *     LIST_APPS\n    -> {"apps":[{"id":..,"name":..,"icon":..}, ...]}
 *     LAUNCH_APP <id>\n -> {"ok":true} | {"ok":false,"error":".."}
 *
 *   "os"/"capabilities" are additive — a client that doesn't look for
 *   them is unaffected. They let a Linux client tell a full syn-relay
 *   Linux/Xorg peer apart from a lightweight Windows/macOS stats-only
 *   agent, and let it hide menu options (e.g. per-window streaming) a
 *   given peer's agent can't actually do, without guessing from the
 *   "os" string alone or hardcoding a per-OS feature table.
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
