/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-CLIENT (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _DEFAULT_SOURCE

#include "syn_local_stats.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>

typedef struct {
	unsigned long long idle, total;
} cpu_snapshot;

static void read_cpu_snapshot(cpu_snapshot *out) {
	out->idle = 0;
	out->total = 0;

	FILE *f = fopen("/proc/stat", "r");
	if (!f) {
		return;
	}
	char line[256];
	if (fgets(line, sizeof(line), f)) {
		unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
		if (sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
			&user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {
			out->idle = idle + iowait;
			out->total = out->idle + user + nice + system + irq + softirq + steal;
		}
	}
	fclose(f);
}

double syn_local_cpu_pct(void) {
	cpu_snapshot a, b;
	read_cpu_snapshot(&a);
	usleep(150000);
	read_cpu_snapshot(&b);

	if (b.total <= a.total) {
		return 0.0;
	}
	unsigned long long total_delta = b.total - a.total;
	unsigned long long idle_delta = b.idle - a.idle;
	if (idle_delta > total_delta) {
		return 0.0;
	}
	return (double)(total_delta - idle_delta) * 100.0 / (double)total_delta;
}

void syn_local_load_avg(double *one, double *five, double *fifteen) {
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

void syn_local_mem_kb(unsigned long long *used_kb, unsigned long long *total_kb) {
	*used_kb = 0;
	*total_kb = 0;

	FILE *f = fopen("/proc/meminfo", "r");
	if (!f) {
		return;
	}

	unsigned long long total = 0, available = 0;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "MemTotal:", 9) == 0) {
			sscanf(line + 9, "%llu", &total);
		} else if (strncmp(line, "MemAvailable:", 13) == 0) {
			sscanf(line + 13, "%llu", &available);
		}
	}
	fclose(f);

	*total_kb = total;
	*used_kb = total > available ? total - available : 0;
}

void syn_local_disk_kb(unsigned long long *used_kb, unsigned long long *total_kb) {
	*used_kb = 0;
	*total_kb = 0;

	struct statvfs st;
	if (statvfs("/", &st) != 0) {
		return;
	}

	unsigned long long total = (unsigned long long)st.f_blocks * st.f_frsize;
	unsigned long long used = total - (unsigned long long)st.f_bfree * st.f_frsize;
	*total_kb = total / 1024;
	*used_kb = used / 1024;
}
