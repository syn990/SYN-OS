/* ------------------------------------------------------------------------
 *                       S Y N - B A R - C P U
 *
 *   Waybar custom/cpu module backend: prints one JSON line (text,
 *   tooltip, class) for local CPU usage, then exits. Waybar re-execs
 *   this on every interval tick (2s per config.jsonc).
 *
 *   This is syn-relay's own local (no-connection) CPU reading — same
 *   /proc/stat delta approach, same JSON shape and threshold classes —
 *   pulled out into its own always-installed binary so custom/cpu keeps
 *   working when syn-relay isn't installed at all, not just when it's
 *   installed but disconnected. syn-relay's --stat-cpu still exists and
 *   still gets used in preference to this one whenever it's present
 *   (see syn-bar-shim) — this binary only covers its absence.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-CPU (Waybar)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <unistd.h>

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

static double cpu_pct(void) {
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

static void load_avg(double *one, double *five, double *fifteen) {
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

int main(void) {
	double one, five, fifteen;
	load_avg(&one, &five, &fifteen);
	double pct = cpu_pct();

	printf("{\"text\": \" %.0f%% \", \"tooltip\": \"Load average: %.2f %.2f %.2f\", \"class\": \"%s\"}\n",
		pct, one, five, fifteen, threshold_class(pct, 70.0, 90.0));
	return 0;
}
