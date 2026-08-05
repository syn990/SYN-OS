/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (screen host role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef CMD_HOST_H
#define CMD_HOST_H

int cmd_host_start(const char *viewer_ip_arg); /* may be NULL/empty — prompts via rofi */
int cmd_host_stop(void);
int cmd_stat_agent(void);

#endif
