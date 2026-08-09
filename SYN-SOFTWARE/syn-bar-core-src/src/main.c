/* ------------------------------------------------------------------------
 *                        S Y N - B A R - C O R E
 *
 *   Persistent daemon behind waybar's syn-bar-* modules: tracks window
 *   title via wlr-foreign-toplevel-management, cpu/mem/disk/ssh/vpn via
 *   periodic /proc reads, and serves it all over a Unix socket at
 *   $XDG_RUNTIME_DIR/syn-bar-core.sock.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-CORE (Waybar)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <mntent.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <sys/wait.h>
#include <wayland-client.h>

#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "syn_bar_tone.h"
#include "syn_stats.h"

typedef struct {
	struct timespec next_due;
	long interval_ms;
	void (*refresh)(void);
} syn_bar_timer;

struct toplevel {
	struct toplevel *next;
	struct zwlr_foreign_toplevel_handle_v1 *handle;
	char *title;
	int activated;
};

static struct toplevel *toplevels = NULL;
static struct toplevel *active = NULL;
static char *current_title = NULL;

/* ---- SYN-RELAY interconnect ----------------------------------------
 * Reads syn-relay's own $XDG_RUNTIME_DIR state files (same formats as
 * syn_node_state.c/syn_agent_state.c/cmd_stats_server.c) — no changes
 * to syn-relay itself. The client-state fetch is a real network round
 * trip (up to 2s), so it runs on its own timer and caches the result;
 * refresh_cpu/mem/disk and the RELAY-STATUS verb just read the cache. */
#define RELAY_INTERVAL_MS 2000
#define RELAY_PORT 47991
#define RELAY_MAX_LINE 1024
#define RELAY_REQUEST_TIMEOUT_SEC 2
#define RELAY_CONNECT_TONE_HZ 1900.0
#define RELAY_TONE_SECONDS 0.2
/* DTMF-style dual tones, matching the digit pairs on a real telephone
 * keypad — digit '0' for a clean disconnect, digit '1' for the remote
 * going unreachable without one. */
#define RELAY_DISCONNECT_DTMF_LOW 941.0
#define RELAY_DISCONNECT_DTMF_HIGH 1336.0
#define RELAY_FAIL_DTMF_LOW 697.0
#define RELAY_FAIL_DTMF_HIGH 1209.0
#define RELAY_DTMF_SECONDS 0.15
/* Digit 'C' on the DTMF grid (852/1633Hz) — the highest, sharpest pair
 * on the whole keypad and unused by any other tone here, so it reads as
 * distinctly more alarming than the calmer 1900Hz CONNECT tone. Someone
 * else's machine actively connected to THIS one's server role is the
 * one state on this whole bar that's genuinely worth interrupting the
 * user for — see write_relay_serving()'s own comment on why this is
 * modeled after Uplink's LanMonitor siren, not TraceTracker's
 * per-window opt-in beep. */
#define INTRUSION_DTMF_LOW 852.0
#define INTRUSION_DTMF_HIGH 1633.0
#define INTRUSION_TONE_SECONDS 0.2

typedef enum {
	RELAY_STATE_ABSENT,
	RELAY_STATE_ACTIVE,
	RELAY_STATE_CONNECTED,
} relay_state;

static relay_state relay_cached_state = RELAY_STATE_ABSENT;
static int relay_was_up = 0; /* CONNECTED-and-reachable, as of the last tick */
static char relay_host[256] = "";
static int relay_reachable = 0;
static char relay_hostname[256] = "";
static double relay_cpu_pct = 0.0;
static unsigned long long relay_mem_used_kb = 0, relay_mem_total_kb = 0;
static unsigned long long relay_disk_used_kb = 0, relay_disk_total_kb = 0;

static void runtime_file_path(const char *filename, char *out, size_t out_size) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	snprintf(out, out_size, "%s/%s", runtime_dir, filename);
}

static void strip_eol(char *s) {
	size_t len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
		s[--len] = '\0';
	}
}

static int pid_alive(long pid) {
	return pid > 0 && kill((pid_t)pid, 0) == 0;
}

/* Reaps syn_bar_tone_play()'s outer fork — it returns immediately and
 * doesn't wait, so without this its children would zombie once done. */
static void handle_sigchld(int sig) {
	(void)sig;
	int saved_errno = errno;
	while (waitpid(-1, NULL, WNOHANG) > 0) {
	}
	errno = saved_errno;
}

static relay_state relay_node_state_get(char *host_out, size_t host_out_size) {
	if (host_out_size > 0) {
		host_out[0] = '\0';
	}
	char path[512];
	runtime_file_path("syn-relay.client-state", path, sizeof(path));

	FILE *f = fopen(path, "r");
	if (!f) {
		return RELAY_STATE_ABSENT;
	}
	char line[256] = {0};
	int has_line = fgets(line, sizeof(line), f) != NULL;
	fclose(f);

	if (!has_line) {
		return RELAY_STATE_ACTIVE;
	}
	strip_eol(line);
	if (line[0] == '\0') {
		return RELAY_STATE_ACTIVE;
	}
	snprintf(host_out, host_out_size, "%s", line);
	return RELAY_STATE_CONNECTED;
}

/* Same wire protocol as syn_relay_conn.c's syn_relay_request(): connect,
 * write "STATS\n", read one JSON line back. */
static int relay_fetch_stats(const char *host, char *reply, size_t reply_size) {
	reply[0] = '\0';

	struct addrinfo hints = {0};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	char port_str[8];
	snprintf(port_str, sizeof(port_str), "%d", RELAY_PORT);

	struct addrinfo *res;
	if (getaddrinfo(host, port_str, &hints, &res) != 0) {
		return 0;
	}

	int fd = -1;
	for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0) {
			continue;
		}
		struct timeval tv = {.tv_sec = RELAY_REQUEST_TIMEOUT_SEC, .tv_usec = 0};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
			break;
		}
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd < 0) {
		return 0;
	}

	const char *cmd = "STATS\n";
	if (write(fd, cmd, strlen(cmd)) < 0) {
		close(fd);
		return 0;
	}
	ssize_t n = read(fd, reply, reply_size - 1);
	close(fd);
	if (n <= 0) {
		return 0;
	}
	reply[n] = '\0';
	strip_eol(reply);
	return 1;
}

/* Hand-rolled, fixed-shape JSON field pull — matches syn_json_extract.c's
 * own rationale (no JSON library for a protocol this repo controls both
 * ends of). */
static int relay_json_number(const char *json, const char *key, double *out) {
	char pattern[64];
	snprintf(pattern, sizeof(pattern), "\"%s\":", key);
	const char *p = strstr(json, pattern);
	if (!p) {
		return 0;
	}
	p += strlen(pattern);
	*out = strtod(p, NULL);
	return 1;
}

static int relay_json_string(const char *json, const char *key, char *out, size_t out_size) {
	char pattern[64];
	snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
	const char *p = strstr(json, pattern);
	if (!p) {
		return 0;
	}
	p += strlen(pattern);
	size_t i = 0;
	while (*p && *p != '"' && i < out_size - 1) {
		out[i++] = *p++;
	}
	out[i] = '\0';
	return 1;
}

static void refresh_relay(void) {
	relay_cached_state = relay_node_state_get(relay_host, sizeof(relay_host));
	relay_reachable = 0;

	if (relay_cached_state != RELAY_STATE_CONNECTED) {
		if (relay_was_up) {
			syn_bar_tone_play_dtmf(RELAY_DISCONNECT_DTMF_LOW, RELAY_DISCONNECT_DTMF_HIGH,
				RELAY_DTMF_SECONDS);
		}
		relay_was_up = 0;
		return;
	}

	char reply[RELAY_MAX_LINE];
	if (!relay_fetch_stats(relay_host, reply, sizeof(reply))) {
		/* State file still says CONNECTED but the fetch failed — the
		 * remote went unreachable without a clean --disconnect. Only
		 * once per outage, not on every failed retry tick. */
		if (relay_was_up) {
			syn_bar_tone_play_dtmf(RELAY_FAIL_DTMF_LOW, RELAY_FAIL_DTMF_HIGH,
				RELAY_DTMF_SECONDS);
		}
		relay_was_up = 0;
		return;
	}
	relay_reachable = 1;
	if (!relay_was_up) {
		syn_bar_tone_play(RELAY_CONNECT_TONE_HZ, RELAY_TONE_SECONDS);
	}
	relay_was_up = 1;

	double cpu = 0, mem_used = 0, mem_total = 0, disk_used = 0, disk_total = 0;
	relay_json_number(reply, "cpu_pct", &cpu);
	relay_json_number(reply, "mem_used_kb", &mem_used);
	relay_json_number(reply, "mem_total_kb", &mem_total);
	relay_json_number(reply, "disk_used_kb", &disk_used);
	relay_json_number(reply, "disk_total_kb", &disk_total);
	relay_cpu_pct = cpu;
	relay_mem_used_kb = (unsigned long long)mem_used;
	relay_mem_total_kb = (unsigned long long)mem_total;
	relay_disk_used_kb = (unsigned long long)disk_used;
	relay_disk_total_kb = (unsigned long long)disk_total;

	if (!relay_json_string(reply, "hostname", relay_hostname, sizeof(relay_hostname)) ||
			!relay_hostname[0]) {
		snprintf(relay_hostname, sizeof(relay_hostname), "%s", relay_host);
	}
}

/* ---- CPU ---------------------------------------------------------- */
#define CPU_INTERVAL_MS 2000

static syn_cpu_snapshot cpu_prev, cpu_cur;
static int cpu_prev_valid = 0;
static char cpu_reply[512] = "{\"text\": \"\", \"tooltip\": \"\"}\n";
static char sysmon_cpu_reply[2048] = "{}\n";

static void cpu_load_avg(double *one, double *five, double *fifteen) {
	*one = *five = *fifteen = 0.0;
	FILE *f = fopen("/proc/loadavg", "r");
	if (!f) {
		return;
	}
	if (fscanf(f, "%lf %lf %lf", one, five, fifteen) != 3) {
		*one = *five = *fifteen = 0.0;
	}
	fclose(f);
}

static const char *threshold_class(double pct, double warning, double critical) {
	if (pct >= critical) {
		return "critical";
	}
	if (pct >= warning) {
		return "warning";
	}
	return "";
}

static void refresh_cpu(void) {
	if (relay_cached_state == RELAY_STATE_ACTIVE || (relay_cached_state == RELAY_STATE_CONNECTED && !relay_reachable)) {
		snprintf(cpu_reply, sizeof(cpu_reply),
			"{\"text\": \" --\", \"tooltip\": \"Relay client active, not connected\", \"class\": \"relay-waiting\"}\n");
		snprintf(sysmon_cpu_reply, sizeof(sysmon_cpu_reply),
			"{\"source\": \"waiting\"}\n");
		return;
	}
	if (relay_cached_state == RELAY_STATE_CONNECTED) {
		snprintf(cpu_reply, sizeof(cpu_reply),
			"{\"text\": \" %.0f%% \", \"tooltip\": \"%s: CPU %.1f%%\", \"class\": \"relay-connected\"}\n",
			relay_cpu_pct, relay_host, relay_cpu_pct);
		snprintf(sysmon_cpu_reply, sizeof(sysmon_cpu_reply),
			"{\"source\": \"remote\", \"host\": \"%s\", \"total_pct\": %.2f}\n",
			relay_hostname, relay_cpu_pct);
		return;
	}

	cpu_prev = cpu_cur;
	syn_stats_cpu_read(&cpu_cur);

	double pct = 0.0;
	if (cpu_prev_valid) {
		pct = syn_stats_cpu_usage(&cpu_prev, &cpu_cur, -1);
	}
	cpu_prev_valid = 1;

	double one, five, fifteen;
	cpu_load_avg(&one, &five, &fifteen);

	snprintf(cpu_reply, sizeof(cpu_reply),
		"{\"text\": \" %.0f%% \", \"tooltip\": \"Load average: %.2f %.2f %.2f\", \"class\": \"%s\"}\n",
		pct, one, five, fifteen, threshold_class(pct, 70.0, 90.0));

	size_t pos = (size_t)snprintf(sysmon_cpu_reply, sizeof(sysmon_cpu_reply),
		"{\"source\": \"local\", \"total_pct\": %.2f, \"load1\": %.2f, \"load5\": %.2f, \"load15\": %.2f, \"cores\": [",
		pct, one, five, fifteen);
	for (int i = 0; i < cpu_cur.core_count && pos < sizeof(sysmon_cpu_reply); i++) {
		double core_pct = cpu_prev_valid ? syn_stats_cpu_usage(&cpu_prev, &cpu_cur, i) : 0.0;
		pos += (size_t)snprintf(sysmon_cpu_reply + pos, sizeof(sysmon_cpu_reply) - pos,
			"%s%.2f", i > 0 ? ", " : "", core_pct);
	}
	snprintf(sysmon_cpu_reply + pos, sizeof(sysmon_cpu_reply) - pos, "]}\n");
}

/* ---- MEM ---------------------------------------------------------- */
#define MEM_INTERVAL_MS 5000

static char mem_reply[512] = "{\"text\": \"\", \"tooltip\": \"\"}\n";
static char sysmon_mem_reply[512] = "{}\n";

static void refresh_mem(void) {
	if (relay_cached_state == RELAY_STATE_ACTIVE || (relay_cached_state == RELAY_STATE_CONNECTED && !relay_reachable)) {
		snprintf(mem_reply, sizeof(mem_reply),
			"{\"text\": \" --\", \"tooltip\": \"Relay client active, not connected\", \"class\": \"relay-waiting\"}\n");
		snprintf(sysmon_mem_reply, sizeof(sysmon_mem_reply), "{\"source\": \"waiting\"}\n");
		return;
	}
	if (relay_cached_state == RELAY_STATE_CONNECTED) {
		double pct = relay_mem_total_kb > 0 ? (double)relay_mem_used_kb * 100.0 / (double)relay_mem_total_kb : 0.0;
		snprintf(mem_reply, sizeof(mem_reply),
			"{\"text\": \" %.0f%% \", \"tooltip\": \"%s: RAM %.1fG / %.1fG\", \"class\": \"relay-connected\"}\n",
			pct, relay_host, relay_mem_used_kb / 1048576.0, relay_mem_total_kb / 1048576.0);
		snprintf(sysmon_mem_reply, sizeof(sysmon_mem_reply),
			"{\"source\": \"remote\", \"host\": \"%s\", \"total_kb\": %llu, \"used_kb\": %llu}\n",
			relay_hostname, relay_mem_total_kb, relay_mem_used_kb);
		return;
	}

	syn_mem_snapshot mem;
	syn_stats_mem_read(&mem);
	unsigned long long used_kb = mem.total_kb > mem.available_kb ? mem.total_kb - mem.available_kb : 0;
	double pct = mem.total_kb > 0 ? (double)used_kb * 100.0 / (double)mem.total_kb : 0.0;

	snprintf(mem_reply, sizeof(mem_reply),
		"{\"text\": \" %.0f%% \", \"tooltip\": \"RAM: %.1fG / %.1fG\", \"class\": \"%s\"}\n",
		pct, used_kb / 1048576.0, mem.total_kb / 1048576.0, threshold_class(pct, 75.0, 90.0));

	snprintf(sysmon_mem_reply, sizeof(sysmon_mem_reply),
		"{\"source\": \"local\", \"total_kb\": %llu, \"free_kb\": %llu, \"available_kb\": %llu, "
		"\"buffers_kb\": %llu, \"cached_kb\": %llu, \"swap_total_kb\": %llu, \"swap_free_kb\": %llu}\n",
		mem.total_kb, mem.free_kb, mem.available_kb, mem.buffers_kb, mem.cached_kb,
		mem.swap_total_kb, mem.swap_free_kb);
}

/* ---- DISK --------------------------------------------------------- */
#define DISK_INTERVAL_MS 15000

static char disk_reply[4300] = "{\"text\": \"\", \"tooltip\": \"\"}\n";

static int disk_is_excluded_fstype(const char *type) {
	static const char *excluded[] = {"tmpfs", "devtmpfs", "squashfs", "overlay"};
	for (size_t i = 0; i < sizeof(excluded) / sizeof(excluded[0]); i++) {
		if (strcmp(type, excluded[i]) == 0) {
			return 1;
		}
	}
	return 0;
}

static void disk_append_json_escaped(char *out, size_t out_size, size_t *len, const char *s) {
	for (const char *p = s; *p && *len < out_size - 1; p++) {
		const char *ins = NULL;
		switch (*p) {
		case '"': ins = "\\\""; break;
		case '\\': ins = "\\\\"; break;
		case '\n': ins = "\\n"; break;
		default:
			out[(*len)++] = *p;
			continue;
		}
		size_t ins_len = strlen(ins);
		if (*len + ins_len >= out_size - 1) {
			break;
		}
		memcpy(out + *len, ins, ins_len);
		*len += ins_len;
	}
}

static void disk_human_size(unsigned long long bytes, char *out, size_t outlen) {
	static const char *units[] = {"B", "K", "M", "G", "T", "P"};
	double size = (double)bytes;
	size_t unit = 0;
	while (size >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
		size /= 1024.0;
		unit++;
	}
	if (unit == 0) {
		snprintf(out, outlen, "%.0f%s", size, units[unit]);
	} else {
		snprintf(out, outlen, "%.1f%s", size, units[unit]);
	}
}

static void refresh_disk(void) {
	if (relay_cached_state == RELAY_STATE_ACTIVE || (relay_cached_state == RELAY_STATE_CONNECTED && !relay_reachable)) {
		snprintf(disk_reply, sizeof(disk_reply),
			"{\"text\": \"DISK --\", \"tooltip\": \"Relay client active, not connected\", \"class\": \"relay-waiting\"}\n");
		return;
	}
	if (relay_cached_state == RELAY_STATE_CONNECTED) {
		snprintf(disk_reply, sizeof(disk_reply),
			"{\"text\": \"%.1fG/%.1fG\", \"tooltip\": \"%s: %.1fG used of %.1fG\", \"class\": \"relay-connected\"}\n",
			relay_disk_used_kb / 1048576.0, relay_disk_total_kb / 1048576.0,
			relay_host, relay_disk_used_kb / 1048576.0, relay_disk_total_kb / 1048576.0);
		return;
	}

	struct statvfs root_st;
	if (statvfs("/", &root_st) != 0) {
		snprintf(disk_reply, sizeof(disk_reply), "{\"text\": \"\", \"tooltip\": \"\"}\n");
		return;
	}

	unsigned long long root_total = (unsigned long long)root_st.f_blocks * root_st.f_frsize;
	unsigned long long root_used = root_total - (unsigned long long)root_st.f_bfree * root_st.f_frsize;
	double root_pct = root_total > 0 ? (double)root_used / (double)root_total * 100.0 : 0.0;

	char used_str[32], total_str[32];
	disk_human_size(root_used, used_str, sizeof(used_str));
	disk_human_size(root_total, total_str, sizeof(total_str));

	FILE *mounts = setmntent("/proc/self/mounts", "r");
	char tooltip[4096] = {0};
	size_t tooltip_len = 0;
	if (mounts) {
		struct mntent *ent;
		while ((ent = getmntent(mounts)) != NULL) {
			if (disk_is_excluded_fstype(ent->mnt_type)) {
				continue;
			}
			struct statvfs st;
			if (statvfs(ent->mnt_dir, &st) != 0 || st.f_blocks == 0) {
				continue;
			}
			unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
			unsigned long long used = total - (unsigned long long)st.f_bfree * st.f_frsize;
			int pct = total > 0 ? (int)((double)used / (double)total * 100.0 + 0.5) : 0;
			char u[32], t[32];
			disk_human_size(used, u, sizeof(u));
			disk_human_size(total, t, sizeof(t));
			int n = snprintf(tooltip + tooltip_len, sizeof(tooltip) - tooltip_len,
				"%-20s %6s / %-6s (%d%%)\n", ent->mnt_dir, u, t, pct);
			if (n < 0 || (size_t)n >= sizeof(tooltip) - tooltip_len) {
				break;
			}
			tooltip_len += (size_t)n;
		}
		endmntent(mounts);
	}
	if (tooltip_len > 0 && tooltip[tooltip_len - 1] == '\n') {
		tooltip[tooltip_len - 1] = '\0';
	}

	size_t pos = (size_t)snprintf(disk_reply, sizeof(disk_reply),
		"{\"text\": \"%s/%s\", \"tooltip\": \"", used_str, total_str);
	disk_append_json_escaped(disk_reply, sizeof(disk_reply), &pos, tooltip);
	pos += (size_t)snprintf(disk_reply + pos, sizeof(disk_reply) - pos,
		"\", \"class\": \"%s\", \"percentage\": %d}\n",
		threshold_class(root_pct, 75.0, 90.0), (int)(root_pct + 0.5));
}

/* ---- VPN ------------------------------------------------------------
 * Plain text, not JSON — matches custom/vpn's format:"" contract. */
#define VPN_INTERVAL_MS 5000

static char vpn_reply[8] = "";
static size_t vpn_reply_len = 0;

static void refresh_vpn(void) {
	if (if_nametoindex("wg0") != 0) {
		vpn_reply_len = (size_t)snprintf(vpn_reply, sizeof(vpn_reply), "on\n");
	} else {
		vpn_reply[0] = '\0';
		vpn_reply_len = 0;
	}
}

/* ---- SSH ------------------------------------------------------------ */
#define SSH_INTERVAL_MS 5000
#define SSH_MAX_SESSIONS 64
#define SSH_TONE_HZ 2600.0
#define SSH_TONE_SECONDS 0.15

struct ssh_session {
	char addr[INET6_ADDRSTRLEN];
	int inbound;
};

static char ssh_seen_inbound[SSH_MAX_SESSIONS][INET6_ADDRSTRLEN];
static int ssh_seen_inbound_count = 0;
static char ssh_reply[2048] = "{\"text\": \"\", \"tooltip\": \"\"}\n";

static int ssh_hex_nibble(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int ssh_was_already_seen(const char *addr) {
	for (int i = 0; i < ssh_seen_inbound_count; i++) {
		if (strcmp(ssh_seen_inbound[i], addr) == 0) {
			return 1;
		}
	}
	return 0;
}

static void refresh_ssh(void) {
	struct ssh_session sessions[SSH_MAX_SESSIONS];
	int count = 0;

	const char *tables[2] = {"/proc/net/tcp", "/proc/net/tcp6"};
	for (int t = 0; t < 2; t++) {
		int is_v6 = (t == 1);
		FILE *f = fopen(tables[t], "r");
		if (!f) continue;

		char line[512];
		if (!fgets(line, sizeof(line), f)) { fclose(f); continue; }

		while (fgets(line, sizeof(line), f)) {
			char local_field[80], rem_field[80], state_hex[8];
			int matched = sscanf(line, " %*d: %79s %79s %7s",
				local_field, rem_field, state_hex);
			if (matched != 3) continue;

			unsigned state = (unsigned)strtoul(state_hex, NULL, 16);
			if (state != 1 /* TCP_ESTABLISHED */) continue;

			char *local_colon = strrchr(local_field, ':');
			if (!local_colon) continue;
			unsigned local_port = (unsigned)strtoul(local_colon + 1, NULL, 16);

			char *colon = strrchr(rem_field, ':');
			if (!colon) continue;
			*colon = '\0';
			const char *rem_addr_hex = rem_field;
			unsigned rem_port = (unsigned)strtoul(colon + 1, NULL, 16);

			int inbound = (local_port == 22);
			if (!inbound && rem_port != 22) continue;

			char addrbuf[INET6_ADDRSTRLEN] = {0};
			if (is_v6) {
				unsigned char raw[16];
				size_t hexlen = strlen(rem_addr_hex);
				if (hexlen != 32) continue;
				for (int w = 0; w < 4; w++) {
					unsigned char word[4];
					for (int b = 0; b < 4; b++) {
						int hi = ssh_hex_nibble(rem_addr_hex[w * 8 + b * 2]);
						int lo = ssh_hex_nibble(rem_addr_hex[w * 8 + b * 2 + 1]);
						if (hi < 0 || lo < 0) { hexlen = 0; break; }
						word[b] = (unsigned char)((hi << 4) | lo);
					}
					if (hexlen == 0) break;
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
					int hi = ssh_hex_nibble(rem_addr_hex[b * 2]);
					int lo = ssh_hex_nibble(rem_addr_hex[b * 2 + 1]);
					if (hi < 0 || lo < 0) { ok = 0; break; }
					raw[3 - b] = (unsigned char)((hi << 4) | lo);
				}
				if (!ok) continue;
				if (!inet_ntop(AF_INET, raw, addrbuf, sizeof(addrbuf))) continue;
			}

			if (count < SSH_MAX_SESSIONS) {
				snprintf(sessions[count].addr, sizeof(sessions[count].addr), "%s", addrbuf);
				sessions[count].inbound = inbound;
				count++;
			}
		}
		fclose(f);
	}

	int any_new_inbound = 0;
	for (int i = 0; i < count; i++) {
		if (sessions[i].inbound && !ssh_was_already_seen(sessions[i].addr)) {
			any_new_inbound = 1;
			break;
		}
	}
	if (any_new_inbound) {
		syn_bar_tone_play(SSH_TONE_HZ, SSH_TONE_SECONDS);
	}

	ssh_seen_inbound_count = 0;
	for (int i = 0; i < count && ssh_seen_inbound_count < SSH_MAX_SESSIONS; i++) {
		if (sessions[i].inbound) {
			snprintf(ssh_seen_inbound[ssh_seen_inbound_count], INET6_ADDRSTRLEN, "%s", sessions[i].addr);
			ssh_seen_inbound_count++;
		}
	}

	if (count == 0) {
		snprintf(ssh_reply, sizeof(ssh_reply), "{\"text\": \"\", \"tooltip\": \"\"}\n");
		return;
	}

	size_t pos = (size_t)snprintf(ssh_reply, sizeof(ssh_reply), "{\"text\": \"ssh: ");
	for (int i = 0; i < count && pos < sizeof(ssh_reply); i++) {
		pos += (size_t)snprintf(ssh_reply + pos, sizeof(ssh_reply) - pos,
			"%s%s %s", i > 0 ? ", " : "", sessions[i].inbound ? "in" : "out", sessions[i].addr);
	}
	pos += (size_t)snprintf(ssh_reply + pos, sizeof(ssh_reply) - pos,
		"\", \"tooltip\": \"Active SSH sessions:");
	for (int i = 0; i < count && pos < sizeof(ssh_reply); i++) {
		pos += (size_t)snprintf(ssh_reply + pos, sizeof(ssh_reply) - pos,
			"\\n%s %s", sessions[i].inbound ? "in from" : "out to", sessions[i].addr);
	}
	snprintf(ssh_reply + pos, sizeof(ssh_reply) - pos, "\", \"class\": \"active\"}\n");
}

/* ---- window-title (wlr-foreign-toplevel-management) ------------------ */

static struct toplevel *toplevel_find(struct zwlr_foreign_toplevel_handle_v1 *handle) {
	for (struct toplevel *t = toplevels; t; t = t->next) {
		if (t->handle == handle) {
			return t;
		}
	}
	return NULL;
}

static void toplevel_remove(struct zwlr_foreign_toplevel_handle_v1 *handle) {
	struct toplevel **link = &toplevels;
	while (*link) {
		if ((*link)->handle == handle) {
			struct toplevel *dead = *link;
			*link = dead->next;
			free(dead->title);
			free(dead);
			return;
		}
		link = &(*link)->next;
	}
}

#define MAX_WATCHERS 8
static int watcher_fds[MAX_WATCHERS];
static int watcher_count = 0;

static void broadcast_title(const char *title) {
	char line[600];
	int len = snprintf(line, sizeof(line), "%s\n", title ? title : "");

	int w = 0;
	while (w < watcher_count) {
		ssize_t n = write(watcher_fds[w], line, (size_t)len);
		if (n != len) {
			watcher_fds[w] = watcher_fds[--watcher_count];
			continue;
		}
		w++;
	}
}

static void set_current_title(const char *title) {
	if (current_title && title && strcmp(current_title, title) == 0) {
		return;
	}
	free(current_title);
	current_title = strdup(title ? title : "");
	broadcast_title(current_title);
}

static void handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
		const char *title) {
	(void)data;
	struct toplevel *t = toplevel_find(handle);
	if (!t) {
		return;
	}
	free(t->title);
	t->title = strdup(title);
}

/* Every opcode needs a real function pointer or libwayland aborts on
 * dispatch, even for events we ignore. */
static void handle_noop(void) {
}

static void handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
		struct wl_array *state) {
	(void)data;
	struct toplevel *t = toplevel_find(handle);
	if (!t) {
		return;
	}
	t->activated = 0;
	uint32_t *entry;
	wl_array_for_each(entry, state) {
		if (*entry == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED) {
			t->activated = 1;
		}
	}
	if (t->activated) {
		active = t;
	}
}

static void handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
	(void)data;
	struct toplevel *t = toplevel_find(handle);
	if (t && t == active) {
		set_current_title(t->title);
	}
}

static void handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
	(void)data;
	int was_active = (active && active->handle == handle);
	zwlr_foreign_toplevel_handle_v1_destroy(handle);
	toplevel_remove(handle);
	if (was_active) {
		active = NULL;
		set_current_title("");
	}
}

static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
	.title = handle_title,
	.app_id = (void (*)(void *, struct zwlr_foreign_toplevel_handle_v1 *,
		const char *))handle_noop,
	.output_enter = (void (*)(void *, struct zwlr_foreign_toplevel_handle_v1 *,
		struct wl_output *))handle_noop,
	.output_leave = (void (*)(void *, struct zwlr_foreign_toplevel_handle_v1 *,
		struct wl_output *))handle_noop,
	.state = handle_state,
	.done = handle_done,
	.closed = handle_closed,
	.parent = (void (*)(void *, struct zwlr_foreign_toplevel_handle_v1 *,
		struct zwlr_foreign_toplevel_handle_v1 *))handle_noop,
};

static void manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
		struct zwlr_foreign_toplevel_handle_v1 *handle) {
	(void)data;
	(void)manager;
	struct toplevel *t = calloc(1, sizeof(*t));
	t->handle = handle;
	t->title = strdup("");
	t->next = toplevels;
	toplevels = t;
	zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, NULL);
}

static void manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager) {
	(void)data;
	zwlr_foreign_toplevel_manager_v1_destroy(manager);
}

static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
	.toplevel = manager_toplevel,
	.finished = manager_finished,
};

static struct zwlr_foreign_toplevel_manager_v1 *toplevel_manager = NULL;

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version) {
	(void)data;
	if (strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name) == 0) {
		toplevel_manager = wl_registry_bind(registry, name,
			&zwlr_foreign_toplevel_manager_v1_interface, version);
		zwlr_foreign_toplevel_manager_v1_add_listener(toplevel_manager,
			&manager_listener, NULL);
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_global,
	.global_remove = (void (*)(void *, struct wl_registry *, uint32_t))handle_noop,
};

/* ---- SYN-RELAY status verbs -------------------------------------------
 * RELAY-STATUS reads from refresh_relay()'s cache (a network fetch, too
 * slow to do per-request). The other three are pure local file/PID
 * reads — cheap enough to do inline, same as PING/TITLE. */

static void write_relay_status(int fd) {
	char reply[512];
	if (relay_cached_state == RELAY_STATE_ABSENT) {
		snprintf(reply, sizeof(reply), "{\"text\": \"\", \"tooltip\": \"\"}\n");
	} else if (relay_cached_state == RELAY_STATE_ACTIVE || !relay_reachable) {
		snprintf(reply, sizeof(reply),
			"{\"text\": \" no connection to remote host\", \"tooltip\": \"%s\", \"class\": \"relay-waiting\"}\n",
			relay_cached_state == RELAY_STATE_ACTIVE
				? "Relay client active, not connected"
				: "Lost connection to remote host");
	} else {
		snprintf(reply, sizeof(reply),
			"{\"text\": \" connected: %s\", \"tooltip\": \"Connected to %s\", \"class\": \"relay-connected\"}\n",
			relay_hostname, relay_host);
	}
	write(fd, reply, strlen(reply));
}

static void write_relay_serving(int fd) {
	char pid_path[512];
	runtime_file_path("syn-relay.server.pid", pid_path, sizeof(pid_path));
	FILE *f = fopen(pid_path, "r");
	long pid_val = 0;
	int running = 0;
	if (f) {
		running = fscanf(f, "%ld", &pid_val) == 1 && pid_alive(pid_val);
		fclose(f);
	}

	char reply[512];
	if (!running) {
		snprintf(reply, sizeof(reply), "{\"text\": \"\", \"tooltip\": \"\"}\n");
		write(fd, reply, strlen(reply));
		return;
	}

	char client_path[512];
	runtime_file_path("syn-relay.server.last-client", client_path, sizeof(client_path));
	char ip[256] = "";
	time_t mtime = 0;
	f = fopen(client_path, "r");
	if (f) {
		if (fgets(ip, sizeof(ip), f)) {
			strip_eol(ip);
		}
		fclose(f);
		struct stat st;
		if (stat(client_path, &st) == 0) {
			mtime = st.st_mtime;
		}
	}

	/* Uplink-style framing: another machine actively polling this one's
	 * stats within the last ~30s is exactly "an intrusion in progress"
	 * from this machine's point of view — unmissable, blinking (see
	 * waybar's style.css .relay-intrusion). Idle stays near-invisible,
	 * same "don't clutter the bar with nothing happening" philosophy as
	 * relay-status's own ABSENT state. Mirrors syn-relay's own (unused
	 * by this waybar module, but kept in sync) cmd_stat_serving(). */
	int fresh = ip[0] && (time(NULL) - mtime <= 30);

	/* This function is called fresh per client request (waybar's own
	 * 5s custom/relay-serving poll interval), not from a shared timer
	 * loop like refresh_relay() — so "did this just start" has to be
	 * tracked here as its own static, comparing against what the LAST
	 * request saw, rather than reusing refresh_relay()'s before/after
	 * pattern directly. Modeled on LanMonitor's siren in the real
	 * Uplink source (fires once per genuinely new attack, not once per
	 * UI poll) rather than TraceTracker's per-window opt-in beep — see
	 * this repo's own design notes on why that distinction matters for
	 * a system-wide security alert. */
	static int was_fresh = 0;
	if (fresh && !was_fresh) {
		syn_bar_tone_play_dtmf(INTRUSION_DTMF_LOW, INTRUSION_DTMF_HIGH, INTRUSION_TONE_SECONDS);
	}
	was_fresh = fresh;

	if (fresh) {
		snprintf(reply, sizeof(reply),
			"{\"text\": \" INTRUSION: %s\", \"tooltip\": \"%s is connected to this machine\", \"class\": \"relay-intrusion\"}\n", ip, ip);
	} else {
		snprintf(reply, sizeof(reply),
			"{\"text\": \" LISTENING\", \"tooltip\": \"syn-relay server role running, no client connected\", \"class\": \"relay-listening\"}\n");
	}
	write(fd, reply, strlen(reply));
}

/* Digit 'A' (697/1633Hz) for starting a watch session — the other
 * unused DTMF corner, distinct from every other tone above. Same
 * per-request transition-tracking shape as write_relay_serving()'s
 * was_fresh, since this function is also called fresh per client
 * request rather than from a shared polling timer. */
#define AGENT_WATCHING_DTMF_LOW 697.0
#define AGENT_WATCHING_DTMF_HIGH 1633.0

static void write_agent_watching(int fd) {
	char pid_path[512], host_path[512];
	runtime_file_path("syn-relay.watch.pid", pid_path, sizeof(pid_path));
	runtime_file_path("syn-relay.watch.host", host_path, sizeof(host_path));

	FILE *f = fopen(pid_path, "r");
	long pid_val = 0;
	char reply[512];
	int watching = f && fscanf(f, "%ld", &pid_val) == 1 && pid_alive(pid_val);
	if (f) fclose(f);

	static int was_watching = 0;
	if (watching && !was_watching) {
		syn_bar_tone_play_dtmf(AGENT_WATCHING_DTMF_LOW, AGENT_WATCHING_DTMF_HIGH, RELAY_DTMF_SECONDS);
	}
	was_watching = watching;

	if (!watching) {
		snprintf(reply, sizeof(reply), "{\"text\": \"\", \"tooltip\": \"\"}\n");
		write(fd, reply, strlen(reply));
		return;
	}

	char host[256] = "";
	f = fopen(host_path, "r");
	if (f) {
		if (fgets(host, sizeof(host), f)) {
			strip_eol(host);
		}
		fclose(f);
	}
	snprintf(reply, sizeof(reply),
		"{\"text\": \" watching: %s\", \"tooltip\": \"SYN-RELAY watching %s\", \"class\": \"agent-watching\"}\n",
		host, host);
	write(fd, reply, strlen(reply));
}

/* Digit 'B' (770/1633Hz) for another machine actually beginning to
 * watch/control THIS one — the most safety-relevant of these four
 * transitions (it's granting a remote peer real input control, not
 * just stats visibility), so it gets its own distinct tone rather than
 * reusing AGENT_WATCHING's. Same was_* transition-tracking shape. */
#define AGENT_SERVING_DTMF_LOW 770.0
#define AGENT_SERVING_DTMF_HIGH 1633.0

static void write_agent_serving(int fd) {
	char pid_path[512];
	runtime_file_path("syn-relay.host.pid", pid_path, sizeof(pid_path));

	FILE *f = fopen(pid_path, "r");
	long video_val = 0, input_val = 0;
	int serving = f && fscanf(f, "%ld %ld", &video_val, &input_val) == 2 &&
		pid_alive(video_val) && pid_alive(input_val);
	if (f) fclose(f);

	static int was_serving = 0;
	if (serving && !was_serving) {
		syn_bar_tone_play_dtmf(AGENT_SERVING_DTMF_LOW, AGENT_SERVING_DTMF_HIGH, RELAY_DTMF_SECONDS);
	}
	was_serving = serving;

	char reply[512];
	if (!serving) {
		snprintf(reply, sizeof(reply), "{\"text\": \"\", \"tooltip\": \"\"}\n");
		write(fd, reply, strlen(reply));
		return;
	}

	char viewer_path[512];
	runtime_file_path("syn-relay.host.viewer", viewer_path, sizeof(viewer_path));
	char viewer[256] = "";
	f = fopen(viewer_path, "r");
	if (f) {
		if (fgets(viewer, sizeof(viewer), f)) {
			strip_eol(viewer);
		}
		fclose(f);
	}
	snprintf(reply, sizeof(reply),
		"{\"text\": \" agent: live\", \"tooltip\": \"SYN-RELAY streaming to %s\", \"class\": \"agent-live\"}\n",
		viewer);
	write(fd, reply, strlen(reply));
}

/* ---- Unix socket ------------------------------------------------------ */

static void socket_path(char *out, size_t out_size) {
	const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
	if (!runtime_dir) {
		runtime_dir = "/tmp";
	}
	snprintf(out, out_size, "%s/syn-bar-core.sock", runtime_dir);
}

static int listen_socket_create(void) {
	char path[256];
	socket_path(path, sizeof(path));
	unlink(path); /* stale socket from a crashed prior run would EADDRINUSE otherwise */

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("syn-bar-core: socket");
		return -1;
	}

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		perror("syn-bar-core: bind");
		close(fd);
		return -1;
	}
	if (listen(fd, 8) != 0) {
		perror("syn-bar-core: listen");
		close(fd);
		return -1;
	}
	return fd;
}

static void handle_one_client(int listen_fd) {
	int client_fd = accept(listen_fd, NULL, NULL);
	if (client_fd < 0) {
		if (errno != EINTR) {
			perror("syn-bar-core: accept");
		}
		return;
	}

	struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
	setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	char request[64] = {0};
	ssize_t n = read(client_fd, request, sizeof(request) - 1);
	if (n <= 0) {
		close(client_fd);
		return;
	}

	if (strncmp(request, "PING", 4) == 0) {
		const char *reply = "PONG\n";
		write(client_fd, reply, strlen(reply));
		close(client_fd);
	} else if (strncmp(request, "TITLE", 5) == 0) {
		char reply[600];
		snprintf(reply, sizeof(reply), "%s\n", current_title ? current_title : "");
		write(client_fd, reply, strlen(reply));
		close(client_fd);
	} else if (strncmp(request, "WATCH-WINDOW-TITLE", 19) == 0) {
		if (watcher_count >= MAX_WATCHERS) {
			close(client_fd);
			return;
		}
		char reply[600];
		snprintf(reply, sizeof(reply), "%s\n", current_title ? current_title : "");
		if (write(client_fd, reply, strlen(reply)) != (ssize_t)strlen(reply)) {
			close(client_fd);
			return;
		}
		watcher_fds[watcher_count++] = client_fd; /* stays open; owned by watcher list now */
	} else if (strncmp(request, "CPU", 3) == 0) {
		write(client_fd, cpu_reply, strlen(cpu_reply));
		close(client_fd);
	} else if (strncmp(request, "MEM", 3) == 0) {
		write(client_fd, mem_reply, strlen(mem_reply));
		close(client_fd);
	} else if (strncmp(request, "DISK", 4) == 0) {
		write(client_fd, disk_reply, strlen(disk_reply));
		close(client_fd);
	} else if (strncmp(request, "VPN", 3) == 0) {
		if (vpn_reply_len > 0) {
			write(client_fd, vpn_reply, vpn_reply_len);
		}
		close(client_fd);
	} else if (strncmp(request, "SSH", 3) == 0) {
		write(client_fd, ssh_reply, strlen(ssh_reply));
		close(client_fd);
	} else if (strncmp(request, "RELAY-STATUS", 12) == 0) {
		write_relay_status(client_fd);
		close(client_fd);
	} else if (strncmp(request, "RELAY-SERVING", 13) == 0) {
		write_relay_serving(client_fd);
		close(client_fd);
	} else if (strncmp(request, "AGENT-WATCHING", 14) == 0) {
		write_agent_watching(client_fd);
		close(client_fd);
	} else if (strncmp(request, "AGENT-SERVING", 13) == 0) {
		write_agent_serving(client_fd);
		close(client_fd);
	} else if (strncmp(request, "SYSMON-CPU", 10) == 0) {
		write(client_fd, sysmon_cpu_reply, strlen(sysmon_cpu_reply));
		close(client_fd);
	} else if (strncmp(request, "SYSMON-MEM", 10) == 0) {
		write(client_fd, sysmon_mem_reply, strlen(sysmon_mem_reply));
		close(client_fd);
	} else if (strncmp(request, "TONE-DTMF ", 10) == 0) {
		/* "TONE-DTMF <hz1> <hz2> <seconds>\n" — this is the single place
		 * in the whole OS that's allowed to call syn_bar_tone_play_dtmf
		 * (see syn_bar_tone.h's own header comment on why: only
		 * syn-bar-core talks to PulseAudio, everything else that wants a
		 * sound asks the bar to make it, the same way everything else
		 * that wants the bar to react already works). No reply expected
		 * — fire-and-forget, same as syn_bar_tone_play itself. */
		double hz1 = 0, hz2 = 0, seconds = 0;
		if (sscanf(request + 10, "%lf %lf %lf", &hz1, &hz2, &seconds) == 3 && seconds > 0) {
			syn_bar_tone_play_dtmf(hz1, hz2, seconds);
		}
		close(client_fd);
	} else if (strncmp(request, "TONE ", 5) == 0) {
		/* "TONE <hz> <seconds>\n" — single-tone counterpart. */
		double hz = 0, seconds = 0;
		if (sscanf(request + 5, "%lf %lf", &hz, &seconds) == 2 && seconds > 0) {
			syn_bar_tone_play(hz, seconds);
		}
		close(client_fd);
	} else {
		const char *reply = "{}\n";
		write(client_fd, reply, strlen(reply));
		close(client_fd);
	}
}

int main(void) {
	struct sigaction sa = {0};
	sa.sa_handler = handle_sigchld;
	sa.sa_flags = SA_RESTART;
	sigaction(SIGCHLD, &sa, NULL);

	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "syn-bar-core: cannot connect to Wayland display\n");
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	wl_display_roundtrip(display);

	if (!toplevel_manager) {
		fprintf(stderr,
			"syn-bar-core: compositor has no "
			"zwlr_foreign_toplevel_manager_v1\n");
		return 1;
	}

	int listen_fd = listen_socket_create();
	if (listen_fd < 0) {
		return 1;
	}

	int wayland_fd = wl_display_get_fd(display);

	syn_bar_timer timers[] = {
		{.interval_ms = RELAY_INTERVAL_MS, .refresh = refresh_relay},
		{.interval_ms = CPU_INTERVAL_MS, .refresh = refresh_cpu},
		{.interval_ms = MEM_INTERVAL_MS, .refresh = refresh_mem},
		{.interval_ms = DISK_INTERVAL_MS, .refresh = refresh_disk},
		{.interval_ms = VPN_INTERVAL_MS, .refresh = refresh_vpn},
		{.interval_ms = SSH_INTERVAL_MS, .refresh = refresh_ssh},
	};
	const int timer_count = (int)(sizeof(timers) / sizeof(timers[0]));

	struct timespec start_now;
	clock_gettime(CLOCK_MONOTONIC, &start_now);
	for (int i = 0; i < timer_count; i++) {
		timers[i].refresh();
		timers[i].next_due = start_now;
		timers[i].next_due.tv_sec += timers[i].interval_ms / 1000;
		timers[i].next_due.tv_nsec += (timers[i].interval_ms % 1000) * 1000000L;
		if (timers[i].next_due.tv_nsec >= 1000000000L) {
			timers[i].next_due.tv_sec += 1;
			timers[i].next_due.tv_nsec -= 1000000000L;
		}
	}

	/* fds layout: 0=Wayland, 1=listen socket, 2..=open watchers. */
	for (;;) {
		while (wl_display_prepare_read(display) != 0) {
			wl_display_dispatch_pending(display);
		}
		wl_display_flush(display);

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		long timeout_ms = -1;
		for (int i = 0; i < timer_count; i++) {
			long ms = (timers[i].next_due.tv_sec - now.tv_sec) * 1000L
				+ (timers[i].next_due.tv_nsec - now.tv_nsec) / 1000000L;
			if (ms < 0) {
				ms = 0;
			}
			if (timeout_ms < 0 || ms < timeout_ms) {
				timeout_ms = ms;
			}
		}

		struct pollfd fds[2 + MAX_WATCHERS];
		fds[0].fd = wayland_fd;
		fds[0].events = POLLIN;
		fds[1].fd = listen_fd;
		fds[1].events = POLLIN;
		/* Snapshot: a watcher registered by handle_one_client() below
		 * this iteration has no slot in fds[] yet, so the cleanup loop
		 * below must stay bounded by this snapshot, not live
		 * watcher_count, or it reads uninitialized revents. */
		int polled_watcher_count = watcher_count;
		for (int i = 0; i < polled_watcher_count; i++) {
			fds[2 + i].fd = watcher_fds[i];
			fds[2 + i].events = POLLIN;
		}
		nfds_t total_fds = (nfds_t)(2 + polled_watcher_count);

		int ready = poll(fds, total_fds, (int)timeout_ms);
		if (ready < 0) {
			if (errno == EINTR) {
				wl_display_cancel_read(display);
				continue;
			}
			perror("syn-bar-core: poll");
			wl_display_cancel_read(display);
			break;
		}

		clock_gettime(CLOCK_MONOTONIC, &now);
		for (int i = 0; i < timer_count; i++) {
			long ms = (timers[i].next_due.tv_sec - now.tv_sec) * 1000L
				+ (timers[i].next_due.tv_nsec - now.tv_nsec) / 1000000L;
			if (ms > 0) {
				continue;
			}
			timers[i].refresh();
			timers[i].next_due.tv_sec += timers[i].interval_ms / 1000;
			timers[i].next_due.tv_nsec += (timers[i].interval_ms % 1000) * 1000000L;
			if (timers[i].next_due.tv_nsec >= 1000000000L) {
				timers[i].next_due.tv_sec += 1;
				timers[i].next_due.tv_nsec -= 1000000000L;
			}
		}

		if (fds[0].revents & POLLIN) {
			wl_display_read_events(display);
			wl_display_dispatch_pending(display);
		} else {
			wl_display_cancel_read(display); /* must match every prepare_read() */
		}

		if (fds[1].revents & POLLIN) {
			handle_one_client(listen_fd);
		}

		for (int i = polled_watcher_count - 1; i >= 0; i--) {
			short rev = fds[2 + i].revents;
			if (!(rev & (POLLIN | POLLHUP | POLLERR))) {
				continue;
			}
			if (rev & POLLIN) {
				char discard[16];
				ssize_t n = read(watcher_fds[i], discard, sizeof(discard));
				if (n > 0) {
					continue;
				}
			}
			close(watcher_fds[i]);
			watcher_fds[i] = watcher_fds[--watcher_count];
		}
	}

	return 0;
}
