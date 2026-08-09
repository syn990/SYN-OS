/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (screen watch role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef CMD_WATCH_H
#define CMD_WATCH_H

int cmd_watch_start(const char *ip_arg); /* ip_arg may be NULL/empty — prompts via the dialpad */
int cmd_watch_stop(void);
int cmd_stat_watching(void);

#endif
