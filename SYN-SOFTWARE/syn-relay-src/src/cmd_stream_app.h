/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef CMD_STREAM_APP_H
#define CMD_STREAM_APP_H

/* Runs `waypipe ssh <ssh_host> <exec>` in the foreground for app `id`,
 * standalone (no --connect session needed). Returns a process exit code. */
int cmd_stream_app(const char *id, const char *ssh_host);

#endif
