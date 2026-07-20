/* ------------------------------------------------------------------------
 *                          S Y N - W I F I
 *
 *   Ncurses Wi-Fi picker talking to iwd directly over D-Bus (see
 *   syn_iwd.h) — no iwctl subprocess, no text parsing. One binary, two
 *   launch contexts: the live installer TTY (no compositor, so this is
 *   the picker, not a rofi popup) and the installed desktop's waybar
 *   Wi-Fi icon (launched in a terminal window in place of the old
 *   rofi-based syn-bar-wifi.zsh).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WIFI (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_iwd.h"
#include "syn_wifi_tui.h"

#include <stdio.h>
#include <string.h>

static bool password_callback(const char *ssid, char *out, size_t out_len, void *userdata) {
	(void)userdata;
	char pw[256];
	if (syn_wifi_tui_password_prompt(ssid, pw, sizeof(pw)) != 0) {
		return false;
	}
	snprintf(out, out_len, "%s", pw);
	memset(pw, 0, sizeof(pw));
	return true;
}

int main(void) {
	sd_bus *bus;
	char device_path[256];
	char err[256] = {0};

	if (!syn_iwd_open(&bus, device_path, sizeof(device_path), err, sizeof(err))) {
		fprintf(stderr, "syn-wifi: %s\n", err);
		return 1;
	}

	syn_wifi_tui_init();

	syn_iwd_network networks[64];
	int count = 0;
	int scanning = 1;

	while (1) {
		if (scanning) {
			syn_iwd_scan(bus, device_path, 15000, err, sizeof(err));
			count = syn_iwd_get_networks(bus, device_path, networks, 64, err, sizeof(err));
			if (count < 0) {
				count = 0;
			}
			scanning = 0;
		}

		int choice = syn_wifi_tui_network_list(networks, count, scanning);
		if (choice == -1) {
			break; /* Esc/q */
		}
		if (choice == -2) {
			scanning = 1; /* 'r' rescan */
			continue;
		}

		const syn_iwd_network *chosen = &networks[choice];
		bool ok = syn_iwd_connect(bus, chosen->object_path, password_callback, NULL, err, sizeof(err));

		char msg[512];
		if (ok) {
			snprintf(msg, sizeof(msg), "Connected to %s", chosen->name);
			syn_wifi_tui_message("Wi-Fi", msg);
			break;
		} else {
			snprintf(msg, sizeof(msg), "Failed to connect to %s: %s", chosen->name, err);
			syn_wifi_tui_message("Wi-Fi", msg);
			/* stay in the list so the user can try another network or
			 * re-enter the password without restarting the whole picker */
		}
	}

	syn_wifi_tui_end();
	syn_iwd_close(bus);
	return 0;
}
