/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "sn_mesh.h"
#include "sn_roster.h"
#include "sn_util.h"

#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

bool sn_secret_load(uint8_t out[32]) {
	char path[512];
	if (!sn_state_path("mesh", path, sizeof(path))) return false;
	uint8_t *data;
	size_t len;
	if (!sn_read_file(path, &data, &len, 64)) return false;
	bool ok = len == 32;
	if (ok) memcpy(out, data, 32);
	sodium_memzero(data, len);
	free(data);
	return ok;
}

bool sn_secret_save(const uint8_t secret[32]) {
	char path[512];
	return sn_state_path("mesh", path, sizeof(path)) && sn_write_file(path, secret, 32, 0600);
}

int sn_addrs_load(sn_addr *out, int max) {
	char path[512];
	if (!sn_state_path("peers", path, sizeof(path))) return 0;
	FILE *f = fopen(path, "r");
	if (!f) return 0;
	int n = 0;
	char line[256];
	while (n < max && fgets(line, sizeof(line), f)) {
		char hex[80], ip[48];
		unsigned port;
		unsigned long long seen;
		if (sscanf(line, "%79s %47s %u %llu", hex, ip, &port, &seen) != 4) continue;
		if (!sn_unhex(hex, out[n].pk, 32) || port == 0 || port > 65535) continue;
		snprintf(out[n].ip, sizeof(out[n].ip), "%s", ip);
		out[n].port = (uint16_t)port;
		out[n].seen = seen;
		n++;
	}
	fclose(f);
	return n;
}

bool sn_addrs_save(const sn_addr *a, int n) {
	char path[512];
	if (!sn_state_path("peers", path, sizeof(path))) return false;
	char *text = malloc((size_t)n * 160 + 1);
	if (!text) return false;
	size_t len = 0;
	for (int i = 0; i < n; i++) {
		char hex[65];
		sn_hex(a[i].pk, 32, hex);
		len += (size_t)snprintf(text + len, 160, "%s %s %u %llu\n", hex, a[i].ip, a[i].port, (unsigned long long)a[i].seen);
	}
	bool ok = sn_write_file(path, text, len, 0600);
	free(text);
	return ok;
}

int sn_addrs_set(sn_addr *a, int n, int max, const uint8_t pk[32], const char *ip, uint16_t port, uint64_t seen) {
	for (int i = 0; i < n; i++) {
		if (memcmp(a[i].pk, pk, 32) == 0) {
			snprintf(a[i].ip, sizeof(a[i].ip), "%s", ip);
			a[i].port = port;
			a[i].seen = seen;
			return n;
		}
	}
	if (n >= max) return n;
	memcpy(a[n].pk, pk, 32);
	snprintf(a[n].ip, sizeof(a[n].ip), "%s", ip);
	a[n].port = port;
	a[n].seen = seen;
	return n + 1;
}

bool sn_mesh_forget(void) {
	char path[512];
	bool ok = sn_roster_clear_disk();
	if (sn_state_path("mesh", path, sizeof(path))) unlink(path);
	if (sn_state_path("peers", path, sizeof(path))) unlink(path);
	return ok;
}
