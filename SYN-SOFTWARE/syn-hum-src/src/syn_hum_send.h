/* ------------------------------------------------------------------------
 *   Lines to syn-bar-core's socket: the HUM levels ten times a second,
 *   HUM-OFF and HUM-TOUR, and HUM-HEARD with its reply. syn_bar_notify.c
 *   does the same for tone meanings and keeps its sender to itself; this
 *   one also says whether syn-bar-core was there.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_HUM_SEND_H
#define SYN_HUM_SEND_H

#include <stddef.h>

/* 0 sent, -1 syn-bar-core isn't listening */
int syn_hum_send(const char *line);

/* Sends `line` and reads the reply into `reply` (NUL-terminated, cut at
 * `len`). Returns the reply's length, or -1 if syn-bar-core isn't there. */
int syn_hum_ask(const char *line, char *reply, size_t len);

#endif
