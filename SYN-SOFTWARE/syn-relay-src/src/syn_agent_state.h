/* ------------------------------------------------------------------------
 *   State files for SYN-RELAY's two screen roles (watch/host), mirroring
 *   syn_node_state.h's shape but kept separate: a machine can be a
 *   stats client/server AND simultaneously watching a peer AND being
 *   watched by another, all independently true — one state file per
 *   role, not one collapsed blob, so no role's read/write can corrupt
 *   another's.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_AGENT_STATE_H
#define SYN_AGENT_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* --watch role: this machine watching a remote peer. */
bool syn_watch_state_save(pid_t view_pid, const char *remote_ip);
/* Returns the tracked PID if it's alive (and copies the remote IP into
 * host_out), or 0 if not currently watching (no state, or the process
 * died without a clean --stop-watching). */
pid_t syn_watch_state_get(char *host_out, size_t host_out_size);
void syn_watch_state_clear(void);

/* --host-watched role: this machine being watched by a remote peer.
 * Two child PIDs (video-send, input-recv) tracked together since they
 * always start/stop as a pair. */
bool syn_host_state_save(pid_t video_pid, pid_t input_pid, const char *viewer_ip);
/* Returns true if both tracked PIDs are alive (a partial pair — one
 * died, one didn't — counts as not-running so callers can cleanly
 * recover). Copies the viewer IP into viewer_out. */
bool syn_host_state_get(pid_t *video_pid_out, pid_t *input_pid_out,
	char *viewer_out, size_t viewer_out_size);
void syn_host_state_clear(void);

#endif
