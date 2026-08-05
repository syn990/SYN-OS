/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef CMD_STATS_CLIENT_H
#define CMD_STATS_CLIENT_H

int cmd_activate(void);
int cmd_connect(const char *host_arg); /* host_arg may be NULL/empty — prompts via rofi */
int cmd_disconnect(void);
int cmd_stat_cpu(void);
int cmd_stat_mem(void);
int cmd_stat_disk(void);
int cmd_stat_relay_status(void);
int cmd_list_apps(void);
int cmd_launch(const char *id);

#endif
