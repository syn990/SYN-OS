/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats server role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef CMD_STATS_SERVER_H
#define CMD_STATS_SERVER_H

int cmd_server_start(void);
int cmd_server_stop(void);
int cmd_server_status(void);
int cmd_stat_serving(void);

#endif
