/* ------------------------------------------------------------------------
 *                       S Y N - B A R - M E M
 *
 *   Waybar custom/mem module backend: prints one JSON line (text,
 *   tooltip, class) for local RAM usage, then exits. Waybar re-execs
 *   this on every interval tick (5s per config.jsonc).
 *
 *   Same rationale as syn-bar-cpu: syn-relay's own local (no-connection)
 *   memory reading, pulled out into its own always-installed binary so
 *   custom/mem keeps working when syn-relay isn't installed at all.
 *   syn-relay's --stat-mem still exists and is still preferred whenever
 *   present (see syn-bar-shim) — this binary only covers its absence.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-MEM (Waybar)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include <stdio.h>
#include <string.h>

static void mem_kb(unsigned long long *used_kb, unsigned long long *total_kb) {
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
	unsigned long long used_kb, total_kb;
	mem_kb(&used_kb, &total_kb);
	double pct = total_kb > 0 ? (double)used_kb * 100.0 / (double)total_kb : 0.0;

	printf("{\"text\": \" %.0f%% \", \"tooltip\": \"RAM: %.1fG / %.1fG\", \"class\": \"%s\"}\n",
		pct, used_kb / 1048576.0, total_kb / 1048576.0, threshold_class(pct, 75.0, 90.0));
	return 0;
}
