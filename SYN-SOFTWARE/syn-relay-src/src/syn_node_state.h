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

/* Marks the client active with no host yet (ACTIVE state). Returns
 * false if the state file can't be written. */
bool syn_node_state_activate(void);

/* Marks the client connected to `host` (CONNECTED state). Returns false
 * if the state file can't be written. */
bool syn_node_state_save(const char *host);

/* Determines the current state. If CONNECTED, also copies the host into
 * `host_out` (up to host_out_size); for ABSENT/ACTIVE, `host_out` is left
 * as an empty string. */
syn_node_state syn_node_state_get(char *host_out, size_t host_out_size);

/* Returns to the ABSENT state (removes the state file entirely). */
void syn_node_state_clear(void);

#endif
