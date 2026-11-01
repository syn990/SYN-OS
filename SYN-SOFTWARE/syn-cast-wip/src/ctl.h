/* ------------------------------------------------------------------------
 *   $XDG_RUNTIME_DIR/syn-cast.sock (or $SYN_CAST_SOCK): one line in, the
 *   answer back, connection closed. It is how a labwc keybind talks while
 *   the window is in the background (`syn-cast ptt toggle`), and how the
 *   tests drive a headless instance.
 *
 *     PTT DOWN | PTT UP | PTT TOGGLE    the talk button
 *     SAY <file.wav>                    talk a recording in
 *     TEXT <message>                    send a text
 *     IMAGE <file.jpg>                  send an image
 *     LOC <lat> <lon> [address]         send a location
 *     CHANNEL <name>                    select a channel
 *     REPLAY | STOP                     replay the last message, stop playback
 *     BUSY | MUTE | VOX | SPEED         toggles, as in the window
 *     OFFLINE | ONLINE                  close the session (the account is free
 *                                       for another client) or open it again
 *     STATUS                            JSON
 *     SNAPSHOT /path.ppm                the next window frame, as a PPM
 *     QUIT
 *
 *   Answers start with "ok" or "error: ...".
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_CTL_H
#define SYN_CAST_CTL_H

#include <stdbool.h>
#include <stddef.h>

#define CTL_MAX_CLIENTS 8

const char *ctl_path(char *out, size_t cap);
/* Binds the socket. Returns the listening fd, -2 if another instance
 * answers on it, -1 on error. */
int ctl_listen(void);
void ctl_close(int fd);
/* poll plumbing for the server side */
int ctl_fds(int listen_fd, int *fds, int max);
void ctl_on_listen(int listen_fd);
void ctl_on_client(int fd);

/* Client: sends one command, prints the answer. Returns the exit code. */
int ctl_call(const char *line);

#endif
