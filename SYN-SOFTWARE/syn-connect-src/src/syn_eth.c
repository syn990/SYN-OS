/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CONNECT (Desktop/Installer)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L
#include "syn_eth.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void set_err(char *err, size_t err_len, const char *fmt, ...) {
	if (!err || err_len == 0) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, err_len, fmt, ap);
	va_end(ap);
}

static bool has_prefix(const char *s, const char *prefix) {
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* True for interfaces this tab shouldn't consider at all: loopback,
 * Wi-Fi (has its own tab), and the common virtual-interface name
 * prefixes this dev machine and most desktop setups create (bridges,
 * VPN tunnels, container veths) — none of those are "the Ethernet
 * port" a user means by an Ethernet status tab. */
static bool should_skip(const char *ifname) {
	if (strcmp(ifname, "lo") == 0) return true;
	if (has_prefix(ifname, "virbr")) return true;
	if (has_prefix(ifname, "docker")) return true;
	if (has_prefix(ifname, "veth")) return true;
	if (has_prefix(ifname, "br-")) return true;
	if (has_prefix(ifname, "tun")) return true;
	if (has_prefix(ifname, "tap")) return true;

	char wireless_path[300];
	snprintf(wireless_path, sizeof(wireless_path), "/sys/class/net/%s/wireless", ifname);
	FILE *f = fopen(wireless_path, "r");
	if (f) {
		fclose(f);
		return true; /* has a wireless/ subdir: this is a Wi-Fi interface */
	}
	return false;
}

static bool read_sysfs_line(const char *ifname, const char *attr, char *out, size_t out_len) {
	char path[300];
	snprintf(path, sizeof(path), "/sys/class/net/%s/%s", ifname, attr);
	FILE *f = fopen(path, "r");
	if (!f) {
		return false;
	}
	bool ok = fgets(out, (int)out_len, f) != NULL;
	fclose(f);
	if (ok) {
		out[strcspn(out, "\n")] = '\0';
	}
	return ok;
}

bool syn_eth_get_status(syn_eth_status *out, char *err, size_t err_len) {
	DIR *d = opendir("/sys/class/net");
	if (!d) {
		set_err(err, err_len, "Could not read /sys/class/net: %s", strerror(errno));
		return false;
	}

	bool found = false;
	struct dirent *entry;
	while (!found && (entry = readdir(d)) != NULL) {
		if (entry->d_name[0] == '.') continue;
		if (should_skip(entry->d_name)) continue;

		memset(out, 0, sizeof(*out));
		snprintf(out->ifname, sizeof(out->ifname), "%s", entry->d_name);

		char operstate[32] = {0};
		read_sysfs_line(entry->d_name, "operstate", operstate, sizeof(operstate));
		out->up = strcmp(operstate, "down") != 0;

		char carrier[8] = {0};
		read_sysfs_line(entry->d_name, "carrier", carrier, sizeof(carrier));
		out->carrier = strcmp(carrier, "1") == 0;

		read_sysfs_line(entry->d_name, "address", out->mac, sizeof(out->mac));

		found = true;
	}
	closedir(d);

	if (!found) {
		set_err(err, err_len, "No Ethernet interface found");
		return false;
	}

	/* IPv4 address, if any — getifaddrs() rather than a subprocess, same
	 * "talk to the real interface, don't shell out" standard as the
	 * Wi-Fi/Bluetooth tabs, just via libc instead of D-Bus since this is
	 * plain kernel interface state with no service in front of it. */
	struct ifaddrs *ifaddr;
	if (getifaddrs(&ifaddr) == 0) {
		for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
			if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
			if (strcmp(ifa->ifa_name, out->ifname) != 0) continue;
			struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
			inet_ntop(AF_INET, &sa->sin_addr, out->ipv4, sizeof(out->ipv4));
			break;
		}
		freeifaddrs(ifaddr);
	}

	return true;
}
