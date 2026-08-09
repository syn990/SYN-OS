/* ------------------------------------------------------------------------
 *   The single stats-client-role state file, shared by every
 *   cmd_stats_client.c subcommand (and, indirectly, by waybar's stat
 *   modules and menu.xml's app-list pipe-menu, since both just invoke
 *   syn-relay rather than reading the file themselves). Three real
 *   states, not two:
 *
 *     ABSENT     — the stats client role has never been activated (or
 *                  was disconnected back to this state). waybar shows
 *                  its own normal local stats, completely unaffected —
 *                  this is the state everyone who's never touched the
 *                  relay feature stays in permanently.
 *     ACTIVE     — activated (menu.xml "Activate Relay Client") but not
 *                  yet connected to any stats server. waybar shows a
 *                  dead/offline indicator, waiting.
 *     CONNECTED  — connected to a real stats server; waybar shows that
 *                  machine's live stats.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_NODE_STATE_H
#define SYN_NODE_STATE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
	SYN_NODE_STATE_ABSENT,
	SYN_NODE_STATE_ACTIVE,
	SYN_NODE_STATE_CONNECTED,
} syn_node_state;

/* One connected peer's STATS-reported "os"/"capabilities" (see
 * syn_relay_protocol.h), cached at connect time so menu.xml pipe-menu
 * generation can hide options the peer can't do without a network
 * round-trip just to ask. */
typedef struct {
	char os[16];             /* "linux" | "windows" | "macos" | "" (unknown) */
	bool apps;
	bool watch_desktop;
	bool watch_window;
	bool host_watched;
} syn_node_caps;

/* Marks the client active with no host yet (ACTIVE state). Returns
 * false if the state file can't be written. */
bool syn_node_state_activate(void);

/* Marks the client connected (CONNECTED state). `stats_host` is used
 * for the direct stats-port (:47991) TCP connect; `ssh_target` is the
 * original alias/string the user typed or picked (before any ssh_config
 * HostName resolution), kept verbatim so a later `ssh`/`waypipe ssh`
 * invocation honors ProxyJump/IdentityFile/etc. the same way typing
 * `ssh <ssh_target>` by hand would. Pass the same string for both if
 * the caller has no meaningful distinction (e.g. a raw IP with no
 * ssh_config entry). `caps` is the STATS reply's "os"/"capabilities" —
 * pass NULL to store an empty/unknown capability set. Returns false if
 * the state file can't be written. */
bool syn_node_state_save(const char *stats_host, const char *ssh_target, const syn_node_caps *caps);

/* Determines the current state. If CONNECTED, also copies the
 * stats-port host into `host_out` (up to host_out_size); for
 * ABSENT/ACTIVE, `host_out` is left as an empty string. This is the
 * common case (waybar stat modules, --launch, --list-apps) that only
 * ever needs the stats-port target, not the SSH target or capabilities
 * — see syn_node_state_get_full() for those. */
syn_node_state syn_node_state_get(char *host_out, size_t host_out_size);

/* Same as syn_node_state_get(), but also copies the original SSH
 * alias/target into `ssh_target_out` and the cached capability set into
 * `caps_out` (either may be NULL if not needed). Used by callers that
 * actually shell out to ssh/waypipe or need to know what the connected
 * peer supports before offering it in a menu. */
syn_node_state syn_node_state_get_full(char *host_out, size_t host_out_size,
	char *ssh_target_out, size_t ssh_target_out_size, syn_node_caps *caps_out);

/* Returns to the ABSENT state (removes the state file entirely). */
void syn_node_state_clear(void);

#endif
