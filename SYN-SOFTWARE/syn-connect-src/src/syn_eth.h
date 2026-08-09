/* ------------------------------------------------------------------------
 *   Ethernet status reader: no daemon to talk to for a wired link (no
 *   NetworkManager, no systemd-networkd on this stack) — a cable is
 *   either plugged in or it isn't, so this is read-only, sourced straight
 *   from sysfs (link/carrier/MAC) and getifaddrs() (IPv4 address), no
 *   subprocess, no D-Bus.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CONNECT (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_ETH_H
#define SYN_ETH_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
	char ifname[16];   /* e.g. "eno1" */
	char mac[24];      /* "AA:BB:CC:DD:EE:FF" */
	char ipv4[16];      /* "" if the link has no IPv4 address (down, or up but no DHCP lease) */
	bool up;           /* administratively up (operstate != "down") */
	bool carrier;      /* cable plugged in and link established */
} syn_eth_status;

/* Fills *out for the first non-loopback, non-wireless, non-virtual
 * Ethernet-family interface found under /sys/class/net (skips wlan*,
 * lo, virbr*, docker*, veth* — anything without a real physical link
 * this tab would meaningfully report on). Returns false if no such
 * interface exists at all (err filled with why). */
bool syn_eth_get_status(syn_eth_status *out, char *err, size_t err_len);

#endif
