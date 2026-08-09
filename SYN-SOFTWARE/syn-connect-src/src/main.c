/* ------------------------------------------------------------------------
 *                        S Y N - C O N N E C T
 *
 *   Tabbed connectivity picker: Wi-Fi (iwd over D-Bus, see syn_iwd.h),
 *   Bluetooth (BlueZ over D-Bus, see syn_bluez.h), Ethernet (read-only
 *   sysfs status, see syn_eth.h), and VPN (openvpn subprocess — no D-Bus
 *   API to talk to, so this is the one tab that really does shell out,
 *   see syn_vpn.h). No iwctl/bluetoothctl subprocess for the first two;
 *   no text parsing anywhere. One binary, two launch contexts: the live
 *   installer TTY (no compositor, so this is the picker, not a rofi
 *   popup) and the installed desktop's waybar Wi-Fi icon (launched in a
 *   terminal window in place of the old rofi-based syn-bar-wifi.zsh).
 *   Formerly syn-wifi; renamed+expanded to also replace the manual
 *   bluetoothctl remove/scan/pair/trust/connect dance for peripherals
 *   like DS4/DualSense controllers with a real picker, plus Ethernet
 *   status and an OpenVPN tab.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CONNECT (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_iwd.h"
#include "syn_bluez.h"
#include "syn_eth.h"
#include "syn_vpn.h"
#include "syn_tui.h"

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

static void wifi_scan_tick(void *userdata) {
	(void)userdata;
	syn_tui_message_spin("Wi-Fi", "Scanning for networks…");
}

static void bt_scan_tick(void *userdata) {
	(void)userdata;
	syn_tui_message_spin("Bluetooth", "Scanning for devices…");
}

static bool wifi_password_callback(const char *ssid, char *out, size_t out_len, void *userdata) {
	(void)userdata;
	char title[300];
	snprintf(title, sizeof(title), "Password for %s", ssid);
	char pw[256];
	if (syn_tui_password_prompt(title, pw, sizeof(pw)) != 0) {
		return false;
	}
	snprintf(out, out_len, "%s", pw);
	memset(pw, 0, sizeof(pw));
	return true;
}

static void build_wifi_rows(const syn_iwd_network *networks, int count, syn_tui_row *out) {
	for (int i = 0; i < count; i++) {
		const syn_iwd_network *n = &networks[i];
		syn_tui_row *r = &out[i];
		memset(r, 0, sizeof(*r));
		snprintf(r->label, sizeof(r->label), "%s", n->name);
		const char *sec = strcmp(n->type, "open") == 0 ? "open" : strcmp(n->type, "psk") == 0 ? "WPA" : strcmp(n->type, "8021x") == 0 ? "802.1x" : n->type;
		snprintf(r->badge, sizeof(r->badge), "%s", sec);
		r->signal_bars = syn_iwd_signal_bars(n->signal_strength);
		r->connected = n->connected;
	}
}

static void build_bt_rows(const syn_bluez_device *devices, int count, syn_tui_row *out) {
	for (int i = 0; i < count; i++) {
		const syn_bluez_device *d = &devices[i];
		syn_tui_row *r = &out[i];
		memset(r, 0, sizeof(*r));
		snprintf(r->label, sizeof(r->label), "%s", d->name);
		/* "trusted"/"paired"/"new" describes saved pairing state, which
		 * doesn't change with the live link — badge it distinctly from
		 * the dashboard's Connected/Available split below, which must
		 * reflect BlueZ's own real-time Connected property or a
		 * trusted-but-idle device (e.g. headphones sitting in their
		 * case) reads identically to one that's actually connected
		 * right now (e.g. a controller mid-session) — confirmed wrong
		 * this way, both showed as "trusted" with no way to tell them
		 * apart. */
		snprintf(r->badge, sizeof(r->badge), "%s", d->connected ? "connected" : (d->paired ? (d->trusted ? "trusted" : "paired") : "new"));
		r->signal_bars = d->rssi != 0 ? syn_bluez_signal_bars(d->rssi) : -1;
		r->connected = d->connected;
	}
}

static void build_vpn_rows(const syn_vpn_config *configs, int count, syn_tui_row *out, bool connected_flags[]) {
	for (int i = 0; i < count; i++) {
		const syn_vpn_config *c = &configs[i];
		syn_tui_row *r = &out[i];
		memset(r, 0, sizeof(*r));
		snprintf(r->label, sizeof(r->label), "%s", c->name);
		bool is_connected = syn_vpn_is_connected(c, NULL, 0);
		snprintf(r->badge, sizeof(r->badge), "%s", is_connected ? "connected" : (c->has_auth_file ? "ready" : "needs auth"));
		r->signal_bars = -1;
		r->connected = is_connected;
		connected_flags[i] = is_connected;
	}
}

int main(void) {
	sd_bus *wifi_bus = NULL;
	char wifi_device[256];
	char wifi_err[256] = {0};
	bool have_wifi = syn_iwd_open(&wifi_bus, wifi_device, sizeof(wifi_device), wifi_err, sizeof(wifi_err));

	sd_bus *bt_bus = NULL;
	char bt_adapter[256];
	char bt_err[256] = {0};
	bool have_bt = syn_bluez_open(&bt_bus, bt_adapter, sizeof(bt_adapter), bt_err, sizeof(bt_err));

	if (!have_wifi && !have_bt) {
		fprintf(stderr, "syn-connect: Wi-Fi: %s\n", wifi_err);
		fprintf(stderr, "syn-connect: Bluetooth: %s\n", bt_err);
		/* No TUI has started yet at this point, so this is the one failure
		 * with no on-screen feedback at all otherwise. Ethernet/VPN never
		 * need a bus connection, so they can't fail this same way. */
		toast("critical", "SYN-Connect", "Neither Wi-Fi (iwd) nor Bluetooth (BlueZ) is available");
		return 1;
	}

	syn_tui_init();

	syn_iwd_network networks[64];
	int wifi_count = 0;
	int wifi_scanning = 0;
	bool wifi_scanned = false;

	syn_bluez_device devices[64];
	int bt_count = 0;
	int bt_scanning = 0;
	bool bt_scanned = false;

	syn_tui_eth_status eth = {0};
	bool eth_scanned = false;
	int eth_scanning = 0;

	syn_vpn_config vpn_configs[64];
	int vpn_count = 0;
	bool vpn_scanned = false;
	int vpn_scanning = 0;
	bool vpn_connected_flags[64] = {0};

	/* No tab scans on launch or on first visit any more — the user
	 * always presses 'r' deliberately. A network/Bluetooth scan is a
	 * multi-second blocking wait (15s each); doing it unasked before the
	 * user has even looked at the screen was surprising, not helpful. */
	syn_tui_tab tab = SYN_TUI_TAB_WIFI;

	syn_tui_row wifi_rows[64], bt_rows[64], vpn_rows[64];

	while (1) {
		if (have_wifi && wifi_scanning) {
			/* Station.Scan() + polling Scanning blocks for real seconds
			 * (see syn_iwd_scan) — draw a visible "scanning" screen before
			 * that call, not just pass scanning=1 into the list screen
			 * for after it returns. Without this the terminal sits blank
			 * for the entire wait, which reads as broken/hung, not busy. */
			syn_tui_message_noinput("Wi-Fi", "Scanning for networks…");
			syn_iwd_scan(wifi_bus, wifi_device, 15000, wifi_scan_tick, NULL, wifi_err, sizeof(wifi_err));
			wifi_count = syn_iwd_get_networks(wifi_bus, wifi_device, networks, 64, wifi_err, sizeof(wifi_err));
			if (wifi_count < 0) wifi_count = 0;
			wifi_scanning = 0;
			wifi_scanned = true;
		}

		if (have_bt && bt_scanning) {
			syn_tui_message_noinput("Bluetooth", "Scanning for devices…");
			/* 15s, matching syn_iwd_scan()'s own timeout above: Classic
			 * Bluetooth inquiry (unlike iwd's Wi-Fi scan) is genuinely
			 * slower to have devices show up in BlueZ's object tree,
			 * especially a previously-bonded device that isn't actively
			 * advertising — 8s left real, in-range devices (confirmed:
			 * a DS4 controller) missing on the first scan. */
			syn_bluez_scan(bt_bus, bt_adapter, 15000, bt_scan_tick, NULL, bt_err, sizeof(bt_err));
			bt_count = syn_bluez_get_devices(bt_bus, bt_adapter, devices, 64, bt_err, sizeof(bt_err));
			if (bt_count < 0) bt_count = 0;
			bt_scanning = 0;
			bt_scanned = true;
		}

		if (eth_scanning) {
			syn_eth_status raw = {0};
			char eth_err[256];
			eth.has_link = syn_eth_get_status(&raw, eth_err, sizeof(eth_err));
			snprintf(eth.ifname, sizeof(eth.ifname), "%s", raw.ifname);
			snprintf(eth.mac, sizeof(eth.mac), "%s", raw.mac);
			snprintf(eth.ipv4, sizeof(eth.ipv4), "%s", raw.ipv4);
			eth.up = raw.up;
			eth.carrier = raw.carrier;
			eth_scanning = 0;
			eth_scanned = true;
		}

		if (vpn_scanning) {
			vpn_count = syn_vpn_list_configs(vpn_configs, 64);
			vpn_scanning = 0;
			vpn_scanned = true;
		}

		build_wifi_rows(networks, wifi_count, wifi_rows);
		build_bt_rows(devices, bt_count, bt_rows);
		build_vpn_rows(vpn_configs, vpn_count, vpn_rows, vpn_connected_flags);

		const syn_tui_row *rows[SYN_TUI_TAB_COUNT] = {wifi_rows, bt_rows, NULL, vpn_rows};
		int row_counts[SYN_TUI_TAB_COUNT] = {wifi_count, bt_count, 0, vpn_count};
		bool scanned[SYN_TUI_TAB_COUNT] = {wifi_scanned, bt_scanned, eth_scanned, vpn_scanned};

		int index = -1;
		int scanning = (tab == SYN_TUI_TAB_WIFI) ? wifi_scanning
		             : (tab == SYN_TUI_TAB_BLUETOOTH) ? bt_scanning
		             : (tab == SYN_TUI_TAB_ETHERNET) ? eth_scanning
		             : vpn_scanning;
		syn_tui_action action = syn_tui_picker(&tab, rows, row_counts, scanned, &eth, scanning, &index);

		if (action == SYN_TUI_ACTION_NONE) {
			break;
		}

		if (action == SYN_TUI_ACTION_RESCAN) {
			switch (tab) {
			case SYN_TUI_TAB_WIFI: if (have_wifi) wifi_scanning = 1; break;
			case SYN_TUI_TAB_BLUETOOTH: if (have_bt) bt_scanning = 1; break;
			case SYN_TUI_TAB_ETHERNET: eth_scanning = 1; break;
			case SYN_TUI_TAB_VPN: vpn_scanning = 1; break;
			default: break;
			}
			continue;
		}

		if (tab == SYN_TUI_TAB_WIFI) {
			if (!have_wifi) continue;
			if (action == SYN_TUI_ACTION_DISCONNECT) {
				bool ok = syn_iwd_disconnect(wifi_bus, wifi_device, wifi_err, sizeof(wifi_err));
				toast(ok ? "normal" : "critical", "Wi-Fi", ok ? "Disconnected" : wifi_err);
				wifi_scanning = 1; /* refresh the list's connected marker */
				continue;
			}
			if (action == SYN_TUI_ACTION_SELECT) {
				const syn_iwd_network *chosen = &networks[index];
				bool ok = syn_iwd_connect(wifi_bus, chosen->object_path, wifi_password_callback, NULL, wifi_err, sizeof(wifi_err));
				char msg[512];
				if (ok) {
					snprintf(msg, sizeof(msg), "Connected to %s", chosen->name);
					toast("normal", "Wi-Fi", msg);
					syn_tui_message("Wi-Fi", msg);
					wifi_scanning = 1;
				} else {
					snprintf(msg, sizeof(msg), "Failed to connect to %s: %s", chosen->name, wifi_err);
					toast("critical", "Wi-Fi", msg);
					syn_tui_message("Wi-Fi", msg);
					/* stay in the list so the user can try another network
					 * or re-enter the password without restarting */
				}
			}
			continue;
		}

		if (tab == SYN_TUI_TAB_BLUETOOTH) {
			if (!have_bt) continue;
			if (action == SYN_TUI_ACTION_DISCONNECT) {
				if (index < 0) continue;
				const syn_bluez_device *chosen = &devices[index];
				bool ok = syn_bluez_disconnect(bt_bus, chosen->object_path, bt_err, sizeof(bt_err));
				toast(ok ? "normal" : "critical", "Bluetooth", ok ? "Disconnected" : bt_err);
				bt_scanning = 1;
				continue;
			}
			if (action == SYN_TUI_ACTION_REMOVE) {
				const syn_bluez_device *chosen = &devices[index];
				bool ok = syn_bluez_remove(bt_bus, bt_adapter, chosen->object_path, bt_err, sizeof(bt_err));
				toast(ok ? "normal" : "critical", "Bluetooth", ok ? "Forgotten" : bt_err);
				bt_scanning = 1;
				continue;
			}
			if (action == SYN_TUI_ACTION_SELECT) {
				const syn_bluez_device *chosen = &devices[index];
				char name[256];
				char device_path[256];
				snprintf(name, sizeof(name), "%s", chosen->name);
				snprintf(device_path, sizeof(device_path), "%s", chosen->object_path);
				bool already_ready = chosen->paired && chosen->trusted;

				char msg[512];
				if (!already_ready) {
					syn_tui_message_noinput("Bluetooth", "Pairing…");
					if (!syn_bluez_pair_and_trust(bt_bus, device_path, bt_err, sizeof(bt_err))) {
						snprintf(msg, sizeof(msg), "Failed to pair %s: %s", name, bt_err);
						toast("critical", "Bluetooth", msg);
						syn_tui_message("Bluetooth", msg);
						bt_scanning = 1;
						continue;
					}
				}

				syn_tui_message_noinput("Bluetooth", "Connecting…");
				bool ok = syn_bluez_connect(bt_bus, device_path, bt_err, sizeof(bt_err));
				if (ok) {
					snprintf(msg, sizeof(msg), "Connected to %s", name);
					toast("normal", "Bluetooth", msg);
					syn_tui_message("Bluetooth", msg);
				} else {
					snprintf(msg, sizeof(msg), "Paired with %s, but connect failed: %s", name, bt_err);
					toast("critical", "Bluetooth", msg);
					syn_tui_message("Bluetooth", msg);
				}
				bt_scanning = 1;
			}
			continue;
		}

		if (tab == SYN_TUI_TAB_VPN) {
			char vpn_err[256];
			if (action == SYN_TUI_ACTION_DISCONNECT) {
				/* `index` is whichever row was selected when 'd' was
				 * pressed, but disconnect only makes sense for the
				 * config that's actually running — fall back to
				 * scanning for it if the selection wasn't already on
				 * it (e.g. cursor sitting on an "Available" row). */
				const syn_vpn_config *target = (index >= 0 && index < vpn_count && vpn_connected_flags[index])
					? &vpn_configs[index] : NULL;
				if (!target) {
					for (int i = 0; i < vpn_count; i++) {
						if (vpn_connected_flags[i]) { target = &vpn_configs[i]; break; }
					}
				}
				if (!target) continue;

				/* Plain `doas kill` prints doas's own password prompt to
				 * whatever its stdin/controlling terminal is — same
				 * terminal ncurses currently owns — so this needs the
				 * same syn_tui_end()/init() bracket syn_vpn_connect()
				 * below does, for the same reason (see syn_vpn.h). */
				syn_tui_end();
				bool ok = syn_vpn_disconnect(target, vpn_err, sizeof(vpn_err));
				syn_tui_init();
				toast(ok ? "normal" : "critical", "VPN", ok ? "Disconnected" : vpn_err);
				vpn_scanning = 1;
				continue;
			}
			if (action == SYN_TUI_ACTION_SELECT) {
				if (index < 0 || index >= vpn_count) continue;
				syn_vpn_config *chosen = &vpn_configs[index];

				if (syn_vpn_needs_credentials(chosen)) {
					/* Pops syn-uplink-dialpad --login itself (a separate
					 * Wayland window) — this ncurses screen keeps running
					 * underneath untouched, no syn_tui_end()/init() needed
					 * here (unlike the plain-doas connect step below). */
					if (!syn_vpn_write_credentials(chosen, vpn_err, sizeof(vpn_err))) {
						syn_tui_message("VPN", vpn_err);
						continue;
					}
					chosen->has_auth_file = true;
				}

				/* openvpn --daemon is a one-shot privileged step (auth,
				 * write pidfile, fork into background, done in under a
				 * second) — not a long-lived interactive program, so a
				 * plain bracketed doas prompt is the right tool, same
				 * "unprivileged binary drops to a plain interactive doas
				 * prompt only for the actual privileged step" pattern
				 * syn-iso-builder's own menu.xml entry already documents.
				 * syn-uplink-dialpad --exec was tried here first and
				 * confirmed wrong: its phase-2 terminal takeover (built
				 * for a full interactive handoff, not a one-shot command)
				 * corrupted this screen once doas authenticated. */
				syn_tui_end();
				printf("\nsyn-connect: connecting to VPN \"%s\" (doas password below if prompted)…\n", chosen->name);
				bool ok = syn_vpn_connect(chosen, vpn_err, sizeof(vpn_err));
				syn_tui_init();

				char msg[512];
				if (ok) {
					snprintf(msg, sizeof(msg), "Connected to %s", chosen->name);
					toast("normal", "VPN", msg);
					syn_tui_message("VPN", msg);
				} else {
					snprintf(msg, sizeof(msg), "Failed to connect to %s: %s", chosen->name, vpn_err);
					toast("critical", "VPN", msg);
					syn_tui_message("VPN", msg);
				}
				vpn_scanning = 1;
			}
			continue;
		}
	}

	syn_tui_end();
	if (have_wifi) syn_iwd_close(wifi_bus);
	if (have_bt) syn_bluez_close(bt_bus);
	return 0;
}
