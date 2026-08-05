/* ------------------------------------------------------------------------
 *                       S Y N - B A R - S S H
 *
 *   Waybar custom/ssh module backend: counts this machine's established
 *   SSH sessions in both directions — outbound (this machine connected
 *   out to a remote sshd, remote port 22) and inbound (a remote machine
 *   connected in to this machine's own sshd, local port 22) — by reading
 *   /proc/net/tcp and /proc/net/tcp6 directly, then prints one JSON line
 *   (text, tooltip, class) and exits. Waybar re-execs this on every
 *   interval tick (5s per config.jsonc). Originally outbound-only; that
 *   meant it stayed silent while someone was actively SSH'd into the
 *   machine, which is exactly the case a general "SSH activity"
 *   indicator should show — widened to catch both directions instead of
 *   splitting into a second module.
 *
 *   Same rationale as syn-bar-vpn/syn-bar-disk: the /proc/net/tcp table
 *   is already exactly what "ss -tn state established '( dport = :22 or
 *   sport = :22 )'" would parse for you, so there's no reason to fork
 *   ss(8) or lsof(8) for a 5-second poll — just read the two tables
 *   ourselves.
 *
 *   Remote address/port are hex-encoded and, for IPv4, byte-swapped
 *   (little-endian word) in /proc/net/tcp; see inet_diag docs / the
 *   kernel's tcp_ipv4.c get_tcp4_sock for the format this mirrors. Local
 *   port is decoded the same way, just to check against SSH_PORT rather
 *   than to render an address.
 *
 *   Waybar renders module text as Pango markup, not plain text — a bare
 *   "<-1.2.3.4" reads as an unclosed tag and GTK silently drops the
 *   whole label (confirmed live: no error, module just never renders).
 *   Directions are spelled out ("in"/"out") instead of arrow glyphs to
 *   sidestep this entirely rather than XML-escape a `<` for no visual
 *   gain.
 *
 *   Each invocation is a fresh, short-lived process with no memory of
 *   the last poll — inbound-connection state (which peer addresses were
 *   already seen) persists across polls via a small file under
 *   $XDG_RUNTIME_DIR, same convention syn-relay's syn_node_state.c
 *   already uses. On seeing a peer address that wasn't in the previous
 *   poll's set, this plays a short 2600Hz tone (the classic phone-phreak
 *   "line just connected" frequency) via syn_bar_tone_play() — see
 *   syn_bar_tone.h for how it reaches the audio sink. syn-relay-src
 *   reuses this same tone module for its own connection events, same
 *   sibling-.c convention syn-audio/syn-wifi already use for
 *   syn_theme.c from syn-crypter-src.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-SSH (Waybar)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "syn_bar_tone.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define TCP_ESTABLISHED 1
#define SSH_PORT 22
#define MAX_SESSIONS 64
#define TONE_HZ 2600.0
#define TONE_SECONDS 0.15

struct ssh_session {
	char addr[INET6_ADDRSTRLEN];
	int inbound; /* 1 if a remote peer connected to our sshd, 0 if we
	              * connected out to theirs */
};

static int hex_nibble(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static void inbound_state_path(char *out, size_t out_size) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	snprintf(out, out_size, "%s/syn-bar-ssh.inbound-seen", runtime_dir);
}

/* Loads the previous poll's inbound peer addresses (one per line) into
 * `seen`, returns how many. Missing file (first-ever run) is treated as
 * "nothing seen yet", not an error. */
static int load_previously_seen(char seen[][INET6_ADDRSTRLEN], int max) {
	char path[512];
	inbound_state_path(path, sizeof(path));

	FILE *f = fopen(path, "r");
	if (!f) {
		return 0;
	}
	int n = 0;
	char line[INET6_ADDRSTRLEN];
	while (n < max && fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
			line[--len] = '\0';
		}
		if (len > 0) {
			snprintf(seen[n], INET6_ADDRSTRLEN, "%s", line);
			n++;
		}
	}
	fclose(f);
	return n;
}

static void save_seen(const struct ssh_session *sessions, int count) {
	char path[512];
	inbound_state_path(path, sizeof(path));

	FILE *f = fopen(path, "w");
	if (!f) {
		return;
	}
	for (int i = 0; i < count; i++) {
		if (sessions[i].inbound) {
			fprintf(f, "%s\n", sessions[i].addr);
		}
	}
	fclose(f);
}

static int was_already_seen(const char seen[][INET6_ADDRSTRLEN], int seen_count, const char *addr) {
	for (int i = 0; i < seen_count; i++) {
		if (strcmp(seen[i], addr) == 0) {
			return 1;
		}
	}
	return 0;
}

int main(void) {
	struct ssh_session sessions[MAX_SESSIONS];
	int count = 0;

	const char *tables[2] = {"/proc/net/tcp", "/proc/net/tcp6"};
	for (int t = 0; t < 2; t++) {
		int is_v6 = (t == 1);
		FILE *f = fopen(tables[t], "r");
		if (!f) continue;

		char line[512];
		/* discard header line */
		if (!fgets(line, sizeof(line), f)) { fclose(f); continue; }

		while (fgets(line, sizeof(line), f)) {
			char local_field[80], rem_field[80], state_hex[8];
			int matched = sscanf(line, " %*d: %79s %79s %7s",
				local_field, rem_field, state_hex);
			if (matched != 3) continue;

			unsigned state = (unsigned)strtoul(state_hex, NULL, 16);
			if (state != TCP_ESTABLISHED) continue;

			char *local_colon = strrchr(local_field, ':');
			if (!local_colon) continue;
			unsigned local_port = (unsigned)strtoul(local_colon + 1, NULL, 16);

			char *colon = strrchr(rem_field, ':');
			if (!colon) continue;
			*colon = '\0';
			const char *rem_addr_hex = rem_field;
			unsigned rem_port = (unsigned)strtoul(colon + 1, NULL, 16);

			int inbound = (local_port == SSH_PORT);
			if (!inbound && rem_port != SSH_PORT) continue;

			char addrbuf[INET6_ADDRSTRLEN] = {0};
			if (is_v6) {
				/* 16 bytes as four little-endian 32-bit words, hex-encoded */
				unsigned char raw[16];
				size_t hexlen = strlen(rem_addr_hex);
				if (hexlen != 32) continue;
				for (int w = 0; w < 4; w++) {
					unsigned char word[4];
					for (int b = 0; b < 4; b++) {
						int hi = hex_nibble(rem_addr_hex[w * 8 + b * 2]);
						int lo = hex_nibble(rem_addr_hex[w * 8 + b * 2 + 1]);
						if (hi < 0 || lo < 0) { hexlen = 0; break; }
						word[b] = (unsigned char)((hi << 4) | lo);
					}
					if (hexlen == 0) break;
					/* kernel stores each 32-bit word host-endian; reverse
					 * the 4 bytes within the word to get network order */
					raw[w * 4 + 0] = word[3];
					raw[w * 4 + 1] = word[2];
					raw[w * 4 + 2] = word[1];
					raw[w * 4 + 3] = word[0];
				}
				if (hexlen == 0) continue;
				if (!inet_ntop(AF_INET6, raw, addrbuf, sizeof(addrbuf))) continue;
			} else {
				unsigned char raw[4];
				size_t hexlen = strlen(rem_addr_hex);
				if (hexlen != 8) continue;
				int ok = 1;
				for (int b = 0; b < 4; b++) {
					int hi = hex_nibble(rem_addr_hex[b * 2]);
					int lo = hex_nibble(rem_addr_hex[b * 2 + 1]);
					if (hi < 0 || lo < 0) { ok = 0; break; }
					/* little-endian word: byte 3 of the address is first
					 * in the hex string */
					raw[3 - b] = (unsigned char)((hi << 4) | lo);
				}
				if (!ok) continue;
				if (!inet_ntop(AF_INET, raw, addrbuf, sizeof(addrbuf))) continue;
			}

			if (count < MAX_SESSIONS) {
				snprintf(sessions[count].addr, sizeof(sessions[count].addr), "%s", addrbuf);
				sessions[count].inbound = inbound;
				count++;
			}
		}
		fclose(f);
	}

	char previously_seen[MAX_SESSIONS][INET6_ADDRSTRLEN];
	int previously_seen_count = load_previously_seen(previously_seen, MAX_SESSIONS);

	int any_new_inbound = 0;
	for (int i = 0; i < count; i++) {
		if (sessions[i].inbound && !was_already_seen(previously_seen, previously_seen_count, sessions[i].addr)) {
			any_new_inbound = 1;
			break;
		}
	}
	if (any_new_inbound) {
		syn_bar_tone_play(TONE_HZ, TONE_SECONDS);
	}
	save_seen(sessions, count);

	if (count == 0) {
		printf("{\"text\": \"\", \"tooltip\": \"\"}\n");
		return 0;
	}

	printf("{\"text\": \"ssh: ");
	for (int i = 0; i < count; i++) {
		if (i > 0) printf(", ");
		printf("%s %s", sessions[i].inbound ? "in" : "out", sessions[i].addr);
	}
	printf("\", \"tooltip\": \"Active SSH sessions:");
	for (int i = 0; i < count; i++) {
		printf("\\n%s %s", sessions[i].inbound ? "in from" : "out to", sessions[i].addr);
	}
	printf("\", \"class\": \"active\"}\n");

	return 0;
}
