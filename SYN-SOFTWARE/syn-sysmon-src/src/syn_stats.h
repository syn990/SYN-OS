/* ------------------------------------------------------------------------
 *   Direct /proc and /sys readers for CPU, memory, and hwmon temperature
 *   sensors — no btop/top subprocess, no shelling out. Mirrors what
 *   waybar's own cpu/memory/temperature modules already sample, so
 *   syn-sysmon's numbers match what the user just clicked on.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_STATS_H
#define SYN_STATS_H

#include <stddef.h>

#define SYN_STATS_MAX_CORES 64
#define SYN_STATS_MAX_SENSORS 32

typedef struct {
	unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
} syn_cpu_ticks;

typedef struct {
	int core_count;                              /* aggregate is index -1, cores are 0..core_count-1 */
	syn_cpu_ticks total;
	syn_cpu_ticks per_core[SYN_STATS_MAX_CORES];
} syn_cpu_snapshot;

/* Reads /proc/stat into *out. Two snapshots a tick apart are required to
 * compute a usage percentage (see syn_stats_cpu_usage) — a single reading
 * of cumulative ticks since boot means nothing on its own. */
void syn_stats_cpu_read(syn_cpu_snapshot *out);

/* Usage percentage of `core` (-1 for the whole-system aggregate) between
 * two snapshots. Returns 0.0 if `core` is out of range for either
 * snapshot (e.g. hot-plugged CPU between reads). */
double syn_stats_cpu_usage(const syn_cpu_snapshot *prev, const syn_cpu_snapshot *cur, int core);

typedef struct {
	unsigned long long total_kb, free_kb, available_kb, buffers_kb, cached_kb;
	unsigned long long swap_total_kb, swap_free_kb;
} syn_mem_snapshot;

/* Reads /proc/meminfo into *out. Fields default to 0 if a key is missing
 * (older kernels lacking MemAvailable, systems with no swap configured). */
void syn_stats_mem_read(syn_mem_snapshot *out);

typedef struct {
	char label[64];  /* hwmon chip name + sensor label, e.g. "nvme: Composite" */
	double celsius;
} syn_sensor_reading;

/* Enumerates every temp*_input under /sys/class/hwmon/hwmon*, filling up
 * to `max` entries into `out`. Returns the count actually filled. A
 * chip/sensor that fails to read (permissions, disappeared between
 * opendir and open) is skipped rather than aborting the whole scan. */
int syn_stats_sensors_read(syn_sensor_reading *out, int max);

#endif
