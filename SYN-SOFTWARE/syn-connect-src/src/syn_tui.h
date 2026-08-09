/* ------------------------------------------------------------------------
 *   ncurses UI for syn-connect: a landing screen (no scan has run yet —
 *   the user picks a tab and presses 'r' to scan, nothing happens
 *   automatically on launch) leading into a tabbed picker (Tab key
 *   cycles Wi-Fi / Bluetooth / Ethernet / VPN) that shows each tab's
 *   results as a dashboard — a "Connected"/"Trusted" section above an
 *   "Available" section, not one flat list — plus a masked password
 *   prompt for Wi-Fi. VPN's username+password credential entry and the
 *   privileged doas step both go through syn-uplink-dialpad's own
 *   graphical popup instead (see syn_vpn.h) — this file has no
 *   credentials screen of its own. Colored from the live SYN-OS theme,
 *   same pattern as syn-crypter's TUI.
 *
 *   This file knows nothing about iwd/BlueZ/openvpn/sysfs — main.c
 *   converts each backend's real data into the generic syn_tui_row
 *   shape below before calling syn_tui_picker(). That keeps this file a
 *   pure renderer and lets a tab be added (this is already the 3rd
 *   iteration: Wi-Fi+Bluetooth, then Ethernet+VPN) without its function
 *   signatures growing a new positional parameter every time.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CONNECT (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TUI_H
#define SYN_TUI_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

void syn_tui_init(void);
void syn_tui_end(void);

typedef enum {
	SYN_TUI_TAB_WIFI = 0,
	SYN_TUI_TAB_BLUETOOTH = 1,
	SYN_TUI_TAB_ETHERNET = 2,
	SYN_TUI_TAB_VPN = 3,
	SYN_TUI_TAB_COUNT = 4,
} syn_tui_tab;

/* One entry in a tab's list. `connected` puts it in the "Connected"/
 * "Trusted" section instead of "Available". `badge` is a short right-
 * aligned status word (security type, "trusted"/"paired"/"new", or
 * "" — not every tab uses it) and `signal_bars` (0-4, or -1 to hide
 * entirely) is the same glyph meter every tab with a real RF signal
 * already drew inline. Callers own the char buffers; syn_tui_picker()
 * only reads them for the duration of one call, nothing is retained. */
typedef struct {
	char label[256];
	char badge[16];
	int signal_bars; /* -1 = don't draw a signal meter for this row */
	bool connected;
} syn_tui_row;

/* Ethernet has no list to pick from (a wired link is either plugged in
 * or it isn't) — its tab renders this fixed status card instead of a
 * row list. `has_link` false means no Ethernet interface was found at
 * all (rest of the fields are then meaningless / left blank). */
typedef struct {
	bool has_link;
	char ifname[16];
	char mac[24];
	char ipv4[16];
	bool up;
	bool carrier;
} syn_tui_eth_status;

/* Result of one pass through syn_tui_picker(): what the user asked for and
 * which list entry (if any) it applies to. The caller (main.c) owns the
 * row arrays and re-populates them (rescan, refresh after a connect,
 * etc.) between calls — this function only ever reads them. */
typedef enum {
	SYN_TUI_ACTION_NONE = 0,   /* Esc/q: caller should quit */
	SYN_TUI_ACTION_SELECT,     /* Enter on index: caller connects/pairs/etc per the active tab */
	SYN_TUI_ACTION_RESCAN,     /* 'r': caller (re)scans/refreshes the active tab and redraws */
	SYN_TUI_ACTION_DISCONNECT, /* 'd': caller disconnects whatever's connected on the active tab */
	SYN_TUI_ACTION_REMOVE,     /* 'x' (bluetooth tab only): caller forgets the selected device */
} syn_tui_action;

/* Renders the active tab (`*tab`, updated in place when the user presses
 * Tab/Shift+Tab so the caller's next call resumes on the same tab) as a
 * dashboard: entries with `connected` set are shown in a "Connected"/
 * "Trusted" section above an "Available" section built from the rest —
 * so what's already usable is always visually separate from what still
 * needs a connect/pair action. The Ethernet tab ignores `rows`/`counts`
 * for its slot and renders `eth` instead (pass NULL if Ethernet has
 * nothing to show yet — same "no scan yet" prompt as the other tabs).
 *
 * `rows[t]`/`row_counts[t]`/`scanned[t]` are indexed by syn_tui_tab.
 * `scanned[t]` false shows a "No scan yet" prompt instead of an empty
 * dashboard — nothing auto-scans on launch or on first tab visit, the
 * user always presses 'r' deliberately. `scanning` shows a spinner/
 * status line while a scan the caller kicked off for the active tab is
 * still in flight — this function itself never blocks on D-Bus/a
 * subprocess, the caller drives scanning between draws. `index`
 * receives the selected row's position within its own tab's `rows[tab]`
 * array for SELECT/DISCONNECT/REMOVE actions (-1 if the list was
 * empty), i.e. the same indexing main.c used when it built that array. */
syn_tui_action syn_tui_picker(syn_tui_tab *tab,
                               const syn_tui_row *rows[SYN_TUI_TAB_COUNT], const int row_counts[SYN_TUI_TAB_COUNT],
                               const bool scanned[SYN_TUI_TAB_COUNT], const syn_tui_eth_status *eth,
                               int scanning, int *index);

/* Masked single-line entry, e.g. a Wi-Fi passphrase. Returns 0 and fills
 * `out` on Enter, -1 on Esc (out left untouched). */
int syn_tui_password_prompt(const char *title, char *out, size_t out_len);

/* Centered message with "press any key to continue" — connect result,
 * errors, etc. */
void syn_tui_message(const char *title, const char *body);

/* Same layout, but draws and returns immediately instead of waiting on a
 * keypress — for a status screen shown right before a real blocking call
 * (e.g. "Scanning…" before the actual scan runs). */
void syn_tui_message_noinput(const char *title, const char *body);

/* Same as syn_tui_message_noinput(), but appends a spinner glyph that
 * advances one frame per call — call this repeatedly (e.g. once per scan
 * poll tick) so a multi-second wait shows visible motion. */
void syn_tui_message_spin(const char *title, const char *body);

#endif
