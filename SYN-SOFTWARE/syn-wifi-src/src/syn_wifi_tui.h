/* ------------------------------------------------------------------------
 *   ncurses UI for syn-wifi: a scrollable network list (signal bars,
 *   security type, connected marker) and a masked password prompt when a
 *   chosen network needs one. Colored from the live SYN-OS theme, same
 *   pattern as syn-crypter's TUI.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WIFI (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_WIFI_TUI_H
#define SYN_WIFI_TUI_H

#include "syn_iwd.h"
#include <stddef.h>

void syn_wifi_tui_init(void);
void syn_wifi_tui_end(void);

/* Renders `networks` (count entries) as a scrollable list — signal bars,
 * security type, and a marker on whatever's currently connected. Enter
 * selects, 'r' requests a rescan (returns -2 so the caller can re-scan
 * and call this again), Esc/q cancels (-1). Returns the chosen index
 * otherwise. `scanning` shows a spinner/status line while a scan the
 * caller kicked off is still in flight (this function itself never
 * blocks on iwd — the caller drives scanning between draws). */
int syn_wifi_tui_network_list(const syn_iwd_network *networks, int count, int scanning);

/* Masked password entry for `ssid`. Returns 0 and fills `out` on Enter,
 * -1 on Esc (out left untouched). */
int syn_wifi_tui_password_prompt(const char *ssid, char *out, size_t out_len);

/* Centered message with "press any key to continue" — connect result,
 * errors, etc. */
void syn_wifi_tui_message(const char *title, const char *body);

#endif
