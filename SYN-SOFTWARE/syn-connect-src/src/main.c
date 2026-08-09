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
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

/* This TUI runs in its own foreground terminal window — the old rofi-based
 * syn-bar-wifi.zsh toasted scan/connect status via mako because a rofi
 * popup had no persistent status area of its own. Here the TUI already
 * shows that status on-screen, but the user may alt-tab away from the
 * terminal while a connect is in flight, so the connect result (the one
 * outcome worth knowing about from elsewhere on the desktop) still gets
 * a toast, same as before. Spawned directly, not via system()/popen(), to
 * avoid a shell for a fixed argv. Best-effort: no mako running (e.g. the
 * live installer TTY, no compositor) just means no toast. */
static void toast(const char *urgency, const char *title, const char *body) {
	pid_t pid;
	char *argv[] = {"notify-send", "-u", (char *)urgency, (char *)title, (char *)body, NULL};
	if (posix_spawnp(&pid, "notify-send", NULL, NULL, argv, environ) == 0) {
		int status;
		waitpid(pid, &status, 0);
	}
}

static void scan_tick(void *userdata) {
	(void)userdata;
	syn_wifi_tui_message_spin("Wi-Fi", "Scanning for networks…");
}

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
		/* No TUI has started yet at this point, so this is the one failure
		 * with no on-screen feedback at all otherwise. */
		toast("critical", "Wi-Fi", err);
		return 1;
	}

	syn_wifi_tui_init();

	syn_iwd_network networks[64];
	int count = 0;
	int scanning = 1;

	while (1) {
		if (scanning) {
			/* Station.Scan() + polling Scanning blocks for real seconds
			 * (see syn_iwd_scan) — draw a visible "scanning" screen before
			 * that call, not just pass scanning=1 into the list screen
			 * for after it returns. Without this the terminal sits blank
			 * for the entire wait, which reads as broken/hung, not busy. */
			syn_wifi_tui_message_noinput("Wi-Fi", "Scanning for networks…");
			syn_iwd_scan(bus, device_path, 15000, scan_tick, NULL, err, sizeof(err));
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
		if (choice == -3) { /* 'd' disconnect */
			bool ok = syn_iwd_disconnect(bus, device_path, err, sizeof(err));
			if (ok) {
				toast("normal", "Wi-Fi", "Disconnected");
			} else {
				toast("critical", "Wi-Fi", err);
			}
			scanning = 1; /* refresh the list's connected marker */
			continue;
		}

		const syn_iwd_network *chosen = &networks[choice];
		bool ok = syn_iwd_connect(bus, chosen->object_path, password_callback, NULL, err, sizeof(err));

		char msg[512];
		if (ok) {
			snprintf(msg, sizeof(msg), "Connected to %s", chosen->name);
			toast("normal", "Wi-Fi", msg);
			syn_wifi_tui_message("Wi-Fi", msg);
			break;
		} else {
			snprintf(msg, sizeof(msg), "Failed to connect to %s: %s", chosen->name, err);
			toast("critical", "Wi-Fi", msg);
			syn_wifi_tui_message("Wi-Fi", msg);
			/* stay in the list so the user can try another network or
			 * re-enter the password without restarting the whole picker */
		}
	}

	syn_wifi_tui_end();
	syn_iwd_close(bus);
	return 0;
}
