/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef CMD_STATS_CLIENT_H
#define CMD_STATS_CLIENT_H

#include "syn_apps.h"

int cmd_activate(void);
int cmd_connect(const char *host_arg); /* host_arg may be NULL/empty — prompts via the dialpad */
int cmd_disconnect(void);
int cmd_stat_cpu(void);
int cmd_stat_mem(void);
int cmd_stat_disk(void);
int cmd_stat_relay_status(void);

/* host_override NULL uses the current CONNECTED node and wires items to
 * --launch (headless); non-NULL queries that host standalone and wires
 * items to --stream-app instead. */
int cmd_list_apps(const char *host_override);
int cmd_launch(const char *id);

/* Array-aware LIST_APPS reply parser — syn_json_string() only finds the
 * first top-level match, not one per array element. Shared by
 * cmd_list_apps() and cmd_stream_app.c. Returns the count filled. */
int syn_apps_parse_wire_reply(const char *reply, syn_app_entry *out, int max);

#endif
