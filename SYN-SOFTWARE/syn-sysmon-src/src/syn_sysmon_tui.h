/* ------------------------------------------------------------------------
 *   ncurses views for syn-sysmon: CPU (per-core bars + aggregate), memory
 *   (used/available/swap), sensors (every hwmon temp reading), and logs
 *   (unit picker + live-streaming journal scrollback). Each view redraws
 *   on a fixed tick and reads input non-blockingly so the numbers stay
 *   live while idle. Colored from the live SYN-OS theme, same pattern as
 *   syn-wifi/syn-crypter's TUIs.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SYSMON_TUI_H
#define SYN_SYSMON_TUI_H

typedef enum {
	SYN_SYSMON_VIEW_CPU,
	SYN_SYSMON_VIEW_MEM,
	SYN_SYSMON_VIEW_SENSORS,
	SYN_SYSMON_VIEW_LOGS,
} syn_sysmon_view;

void syn_sysmon_tui_init(void);
void syn_sysmon_tui_end(void);

/* Runs `view` until the user quits (q/Esc) or switches views with Tab —
 * returns the next view to show on Tab, or -1 on quit. Blocks internally
 * on a timed getch() (1s tick) so it can't spin the CPU it's reporting
 * on. */
int syn_sysmon_tui_run(syn_sysmon_view view);

#endif
