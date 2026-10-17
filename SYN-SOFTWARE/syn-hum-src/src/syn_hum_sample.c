/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_hum_sample.h"

#include "syn_journal.h"
#include "syn_stats.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_DISKS 32

#define TCP_ESTABLISHED 0x01
#define TCP_SYN_RECV 0x03
#define TCP_LISTEN 0x0A

typedef struct {
	char name[32];
	unsigned long long io_ms;
} disk_time;

typedef struct {
	uint64_t *keys; /* sorted */
	size_t n, cap;
} key_set;

struct syn_hum_sampler {
	struct timespec when;
	syn_cpu_snapshot cpu;
	int has_psi;
	unsigned long long stall_us, cpu_wait_us, io_wait_us;
	unsigned long long spawns, switches;
	unsigned long long packets;
	disk_time disks[MAX_DISKS];
	int disk_count;
	unsigned long long disk_sectors;
	key_set inbound, reading; /* last sample's connections, and the buffer the next one is read into */
	int has_snmp;
	unsigned long long passive_opens; /* the kernel's count of connections that came in */
	sd_journal *log;
};

static double clamp01(double v) {
	return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
}

static int grow(void **array, size_t *cap, size_t need, size_t size) {
	if (need <= *cap) {
		return 0;
	}
	size_t grown_cap = *cap ? *cap * 2 : 64;
	void *grown = realloc(*array, grown_cap * size);
	if (!grown) {
		return -1;
	}
	*array = grown;
	*cap = grown_cap;
	return 0;
}

/* The "some" line's total: microseconds at least one task spent waiting
 * for memory, a CPU or IO (`what`), since boot. Fails without CONFIG_PSI,
 * or with psi=0 on the kernel command line (the file is there but won't
 * read). */
static int read_pressure(const char *what, unsigned long long *us) {
	char path[64];
	snprintf(path, sizeof(path), "/proc/pressure/%s", what);
	FILE *f = fopen(path, "r");
	if (!f) {
		return -1;
	}
	int found = -1;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		const char *total = strstr(line, "total=");
		if (strncmp(line, "some", 4) == 0 && total) {
			*us = strtoull(total + 6, NULL, 10);
			found = 0;
			break;
		}
	}
	fclose(f);
	return found;
}

/* Processes started and context switches since boot */
static void read_operations(unsigned long long *spawns, unsigned long long *switches) {
	FILE *f = fopen("/proc/stat", "r");
	if (!f) {
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "ctxt ", 5) == 0) {
			*switches = strtoull(line + 5, NULL, 10);
		} else if (strncmp(line, "processes ", 10) == 0) {
			*spawns = strtoull(line + 10, NULL, 10);
		}
	}
	fclose(f);
}

/* What a pressure counter says about the time since it was last read:
 * 0-1, or -1 if it can't be read */
static double pressure_share(const char *what, unsigned long long *last_us, double seconds) {
	unsigned long long us;
	if (read_pressure(what, &us) != 0) {
		return -1.0;
	}
	double share = us >= *last_us ? clamp01((double)(us - *last_us) / (seconds * 1e6)) : 0.0;
	*last_us = us;
	return share;
}

static unsigned long long read_packets(void) {
	FILE *f = fopen("/proc/net/dev", "r");
	if (!f) {
		return 0;
	}
	unsigned long long total = 0;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char *colon = strchr(line, ':');
		if (!colon) {
			continue; /* the two header lines */
		}
		*colon = '\0';
		const char *name = line;
		while (*name == ' ') {
			name++;
		}
		if (strcmp(name, "lo") == 0) {
			continue;
		}
		unsigned long long rx, tx;
		if (sscanf(colon + 1, "%*u %llu %*u %*u %*u %*u %*u %*u %*u %llu", &rx, &tx) == 2) {
			total += rx + tx;
		}
	}
	fclose(f);
	return total;
}

/* A whole physical disk has a device link in /sys/block. Partitions,
 * loop, dm, md and zram don't, and counting them would count the same IO
 * twice. /sys/block spells a slash in a name as '!'. */
static int is_disk(const char *name) {
	char path[96] = "/sys/block/";
	size_t len = strlen(path);
	for (const char *p = name; *p && len < sizeof(path) - sizeof("/device"); p++) {
		path[len++] = *p == '/' ? '!' : *p;
	}
	memcpy(path + len, "/device", sizeof("/device"));
	return access(path, F_OK) == 0;
}

/* Each disk's milliseconds spent doing IO, and every disk's sectors read
 * and written together (always 512 bytes in /proc/diskstats) */
static int read_disks(disk_time *out, int max, unsigned long long *sectors) {
	*sectors = 0;
	FILE *f = fopen("/proc/diskstats", "r");
	if (!f) {
		return 0;
	}
	int n = 0;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char name[32];
		unsigned long long read_sectors, written_sectors, io_ms;
		if (sscanf(line, "%*u %*u %31s %*u %*u %llu %*u %*u %*u %llu %*u %*u %llu",
		           name, &read_sectors, &written_sectors, &io_ms) != 4 || !is_disk(name)) {
			continue;
		}
		*sectors += read_sectors + written_sectors;
		if (n < max) {
			memcpy(out[n].name, name, sizeof(name));
			out[n].io_ms = io_ms;
			n++;
		}
	}
	fclose(f);
	return n;
}

typedef struct {
	unsigned char local[16], remote[16];
	int len; /* 4 or 16 */
	unsigned lport, rport, state;
} tcp_conn;

/* The kernel prints an address as 32-bit words in host order, so each
 * word parsed and stored back in host order gives the address's bytes.
 * Returns the address's length, 4 or 16. */
static int parse_addr(const char *hex, unsigned char out[16]) {
	size_t len = strlen(hex);
	if (len != 8 && len != 32) {
		return -1;
	}
	memset(out, 0, 16);
	for (size_t i = 0; i < len / 8; i++) {
		char word_hex[9];
		memcpy(word_hex, hex + i * 8, 8);
		word_hex[8] = '\0';
		uint32_t word = (uint32_t)strtoul(word_hex, NULL, 16);
		memcpy(out + i * 4, &word, 4);
	}
	return (int)len / 2;
}

static int is_loopback(const unsigned char *a, int len) {
	static const unsigned char v6_loopback[16] = {[15] = 1};
	static const unsigned char v4_mapped[12] = {[10] = 0xff, [11] = 0xff};
	if (len == 4) {
		return a[0] == 127;
	}
	return memcmp(a, v6_loopback, 16) == 0 || (memcmp(a, v4_mapped, 12) == 0 && a[12] == 127);
}

static void read_tcp(const char *path, tcp_conn **list, size_t *n, size_t *cap) {
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		char local[33], remote[33];
		tcp_conn c;
		if (sscanf(line, " %*u: %32[0-9A-Fa-f]:%x %32[0-9A-Fa-f]:%x %x",
		           local, &c.lport, remote, &c.rport, &c.state) != 5) {
			continue; /* the header */
		}
		c.len = parse_addr(local, c.local);
		if (c.len < 0 || parse_addr(remote, c.remote) != c.len || c.lport > 0xFFFF) {
			continue;
		}
		if (grow((void **)list, cap, *n + 1, sizeof(tcp_conn)) < 0) {
			break;
		}
		(*list)[(*n)++] = c;
	}
	fclose(f);
}

/* FNV-1a over both ends: the same connection keeps its key from SYN_RECV
 * to ESTABLISHED */
static uint64_t conn_key(const tcp_conn *c) {
	struct {
		unsigned char local[16], remote[16];
		unsigned lport, rport;
	} ends;
	memset(&ends, 0, sizeof(ends));
	memcpy(ends.local, c->local, 16);
	memcpy(ends.remote, c->remote, 16);
	ends.lport = c->lport;
	ends.rport = c->rport;
	uint64_t h = 1469598103934665603ULL;
	const unsigned char *p = (const unsigned char *)&ends;
	for (size_t i = 0; i < sizeof(ends); i++) {
		h = (h ^ p[i]) * 1099511628211ULL;
	}
	return h;
}

static int by_key(const void *a, const void *b) {
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

/* Connections from another machine: half-open ones (SYN_RECV, where a
 * port scan shows), and established ones on a port something here
 * listens on. One that opens and closes between two samples isn't seen. */
static void read_inbound(key_set *out) {
	static unsigned char listening[65536 / 8];
	tcp_conn *list = NULL;
	size_t n = 0, cap = 0;
	read_tcp("/proc/net/tcp", &list, &n, &cap);
	read_tcp("/proc/net/tcp6", &list, &n, &cap);

	memset(listening, 0, sizeof(listening));
	for (size_t i = 0; i < n; i++) {
		if (list[i].state == TCP_LISTEN) {
			listening[list[i].lport / 8] |= (unsigned char)(1 << (list[i].lport % 8));
		}
	}

	out->n = 0;
	for (size_t i = 0; i < n; i++) {
		const tcp_conn *c = &list[i];
		int on_listener = listening[c->lport / 8] & (1 << (c->lport % 8));
		if (!(c->state == TCP_SYN_RECV || (c->state == TCP_ESTABLISHED && on_listener)) || is_loopback(c->remote, c->len)) {
			continue;
		}
		if (grow((void **)&out->keys, &out->cap, out->n + 1, sizeof(uint64_t)) < 0) {
			break;
		}
		out->keys[out->n++] = conn_key(c);
	}
	free(list);
	if (out->n) {
		qsort(out->keys, out->n, sizeof(uint64_t), by_key);
	}
}

/* Tcp's PassiveOpens in /proc/net/snmp: every connection that has come in
 * since boot, half-open ones (a port scan) included. Reading it costs a
 * few hundredths of a millisecond; reading /proc/net/tcp costs the
 * kernel a walk of its whole connection table, half a millisecond or
 * more however few connections there are. So the table is read only
 * when this count has moved. The first Tcp: line names the fields, the
 * second has their values. */
static int read_passive_opens(unsigned long long *out) {
	FILE *f = fopen("/proc/net/snmp", "r");
	if (!f) return -1;
	char names[1024], values[1024];
	int found = -1;
	while (fgets(names, sizeof(names), f)) {
		if (strncmp(names, "Tcp:", 4) != 0) continue;
		if (!fgets(values, sizeof(values), f)) break;
		char *ns = NULL, *vs = NULL;
		char *n = strtok_r(names, " \n", &ns), *v = strtok_r(values, " \n", &vs);
		for (; n && v; n = strtok_r(NULL, " \n", &ns), v = strtok_r(NULL, " \n", &vs)) {
			if (strcmp(n, "PassiveOpens") == 0) {
				*out = strtoull(v, NULL, 10);
				found = 0;
				break;
			}
		}
		break;
	}
	fclose(f);
	return found;
}

static int count_new(const key_set *before, const key_set *now) {
	int fresh = 0;
	for (size_t i = 0; i < now->n; i++) {
		if (!before->n || !bsearch(&now->keys[i], before->keys, before->n, sizeof(uint64_t), by_key)) {
			fresh++;
		}
	}
	return fresh;
}

/* Every log line since the last call, counting those at error priority
 * or worse: the journal's PRIORITY 0-3, or in a plain log file a line
 * syn-sysmon's reader guesses is one (it says error, fail, crit ...) */
static int read_log_errors(sd_journal *j) {
	static syn_journal_entry batch[128];
	if (!j || !syn_journal_has_new(j)) {
		return 0;
	}
	int errors = 0, got;
	do {
		got = syn_journal_read_new(j, batch, 128);
		for (int i = 0; i < got; i++) {
			if (batch[i].priority <= 3) {
				errors++;
			}
		}
	} while (got == 128);
	return errors;
}

syn_hum_sampler *syn_hum_sampler_open(void) {
	syn_hum_sampler *s = calloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}
	clock_gettime(CLOCK_MONOTONIC, &s->when);
	syn_stats_cpu_read(&s->cpu);
	s->has_psi = read_pressure("memory", &s->stall_us) == 0;
	read_pressure("cpu", &s->cpu_wait_us);
	read_pressure("io", &s->io_wait_us);
	read_operations(&s->spawns, &s->switches);
	s->packets = read_packets();
	s->disk_count = read_disks(s->disks, MAX_DISKS, &s->disk_sectors);
	s->has_snmp = read_passive_opens(&s->passive_opens) == 0;
	read_inbound(&s->inbound);
	s->log = syn_journal_open(NULL, 0);
	read_log_errors(s->log); /* what was already there isn't news */
	return s;
}

void syn_hum_sampler_close(syn_hum_sampler *s) {
	if (!s) {
		return;
	}
	syn_journal_close(s->log);
	free(s->inbound.keys);
	free(s->reading.keys);
	free(s);
}

int syn_hum_sampler_has_psi(const syn_hum_sampler *s) {
	return s->has_psi;
}

int syn_hum_sampler_has_logs(const syn_hum_sampler *s) {
	return s->log != NULL;
}

void syn_hum_sample(syn_hum_sampler *s, syn_hum_signals *out) {
	memset(out, 0, sizeof(*out));

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	out->seconds = (double)(now.tv_sec - s->when.tv_sec) + (double)(now.tv_nsec - s->when.tv_nsec) / 1e9;
	if (out->seconds < 1e-3) {
		out->seconds = 1e-3;
	}
	s->when = now;

	syn_cpu_snapshot cpu;
	syn_stats_cpu_read(&cpu);
	out->cpu = syn_stats_cpu_usage(&s->cpu, &cpu, -1) / 100.0;
	out->core_count = cpu.core_count < SYN_HUM_MAX_CORES ? cpu.core_count : SYN_HUM_MAX_CORES;
	for (int i = 0; i < out->core_count; i++) {
		out->core[i] = syn_stats_cpu_usage(&s->cpu, &cpu, i) / 100.0;
	}
	s->cpu = cpu;

	out->mem_stall = out->cpu_wait = out->io_wait = -1.0;
	if (s->has_psi) {
		out->mem_stall = pressure_share("memory", &s->stall_us, out->seconds);
		out->cpu_wait = pressure_share("cpu", &s->cpu_wait_us, out->seconds);
		out->io_wait = pressure_share("io", &s->io_wait_us, out->seconds);
	}

	unsigned long long spawns = s->spawns, switches = s->switches;
	read_operations(&spawns, &switches);
	out->spawns = spawns > s->spawns ? spawns - s->spawns : 0;
	out->switches = switches > s->switches ? switches - s->switches : 0;
	s->spawns = spawns;
	s->switches = switches;

	/* An interface going away takes its count with it: no negative packets */
	unsigned long long packets = read_packets();
	out->packets = packets > s->packets ? packets - s->packets : 0;
	s->packets = packets;

	disk_time disks[MAX_DISKS];
	unsigned long long sectors;
	int disk_count = read_disks(disks, MAX_DISKS, &sectors);
	for (int i = 0; i < disk_count; i++) {
		for (int k = 0; k < s->disk_count; k++) {
			if (strcmp(disks[i].name, s->disks[k].name) == 0 && disks[i].io_ms >= s->disks[k].io_ms) {
				double busy = clamp01((double)(disks[i].io_ms - s->disks[k].io_ms) / (out->seconds * 1000.0));
				if (busy > out->disk_busy) {
					out->disk_busy = busy;
				}
				break;
			}
		}
	}
	out->disk_bytes = sectors > s->disk_sectors ? (sectors - s->disk_sectors) * 512 : 0;
	memcpy(s->disks, disks, sizeof(disks[0]) * (size_t)disk_count);
	s->disk_count = disk_count;
	s->disk_sectors = sectors;

	/* The connection table only when something has come in (or every time,
	 * on a kernel without /proc/net/snmp) */
	unsigned long long passive = 0;
	if (!s->has_snmp || read_passive_opens(&passive) != 0 || passive != s->passive_opens) {
		s->passive_opens = passive;
		read_inbound(&s->reading);
		out->inbound = count_new(&s->inbound, &s->reading);
		key_set swap = s->inbound;
		s->inbound = s->reading;
		s->reading = swap;
	}

	out->log_errors = read_log_errors(s->log);
}
