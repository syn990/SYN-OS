/* ------------------------------------------------------------------------
 *                          S Y N - S Y S M O N
 *
 *   Native system monitor for SYN-OS's waybar stats — one binary, one
 *   view per launch (CPU/memory/sensors/logs), selected with --view= so
 *   each waybar module's on-click deep-links straight to the stat it
 *   represents instead of dumping every click into the same generic
 *   btop screen. Reads /proc and /sys directly (see syn_stats.h) and the
 *   journal directly via sd-journal (see syn_journal.h) — no btop/top/
 *   journalctl subprocess. Tab switches views without leaving the TUI.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_sysmon_tui.h"

#include <stdio.h>
#include <string.h>

static int parse_view(const char *arg) {
	if (strcmp(arg, "cpu") == 0) {
		return SYN_SYSMON_VIEW_CPU;
	}
	if (strcmp(arg, "mem") == 0 || strcmp(arg, "ram") == 0) {
		return SYN_SYSMON_VIEW_MEM;
	}
	if (strcmp(arg, "sensors") == 0 || strcmp(arg, "temp") == 0) {
		return SYN_SYSMON_VIEW_SENSORS;
	}
	if (strcmp(arg, "logs") == 0) {
		return SYN_SYSMON_VIEW_LOGS;
	}
	return -1;
}

int main(int argc, char **argv) {
	int view = SYN_SYSMON_VIEW_CPU;

	for (int i = 1; i < argc; i++) {
		const char *prefix = "--view=";
		size_t prefix_len = strlen(prefix);
		if (strncmp(argv[i], prefix, prefix_len) == 0) {
			int parsed = parse_view(argv[i] + prefix_len);
			if (parsed < 0) {
				fprintf(stderr, "syn-sysmon: unknown view '%s' (expected cpu, mem, sensors, or logs)\n", argv[i] + prefix_len);
				return 1;
			}
			view = parsed;
		} else {
			fprintf(stderr, "syn-sysmon: unknown argument '%s'\n", argv[i]);
			return 1;
		}
	}

	syn_sysmon_tui_init();
	while (view >= 0) {
		view = syn_sysmon_tui_run((syn_sysmon_view)view);
	}
	syn_sysmon_tui_end();
	return 0;
}
