/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_hum_map.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* A pressure share (0-1, -1 unknown) on the log scale steps use */
static double pressure_level(double share) {
	share = share > 0.0 ? share : 0.0;
	return log10(1.0 + 1000.0 * share) / log10(1001.0);
}

static int stepper_feed(syn_hum_stepper *s, double value) {
	if (value < 0.0) value = 0.0;
	if (value > 1.0) value = 1.0;
	s->level += 0.22 * (value - s->level);
	double target = s->level * 15.0;
	if (target > s->step + 0.7 || target < s->step - 0.7) {
		s->step = (int)lround(target);
	}
	return s->step;
}

void syn_hum_map_signals(syn_hum_map *m, const syn_hum_signals *sig, syn_hum_steps *out) {
	memset(out, 0, sizeof(*out));
	out->core_count = sig->core_count < SYN_HUM_MAP_CORES ? sig->core_count : SYN_HUM_MAP_CORES;
	for (int i = 0; i < out->core_count; i++) {
		out->cores[i] = stepper_feed(&m->cores[i], sig->core[i]);
	}
	/* Stalls that matter are small: 0.1% is step 1, 1% step 5, 10% step
	 * 10. Waiting for a CPU or on IO the same way. */
	out->stall = stepper_feed(&m->stall, pressure_level(sig->mem_stall));
	out->cpu_wait = stepper_feed(&m->cpu_wait, pressure_level(sig->cpu_wait));
	out->io_wait = stepper_feed(&m->io_wait, pressure_level(sig->io_wait));
	/* Processes started: 1 a second is step 1, 30 step 7, 1000 step 15 */
	out->spawn = stepper_feed(&m->spawn, log2(1.0 + (double)sig->spawns / sig->seconds) / 10.0);
	/* Context switches: 1000 a second is step 0, a desktop's ten
	 * thousand step 5 or 6, 100000 step 11, 600000 step 15 */
	out->sched = stepper_feed(&m->sched, (log10(1.0 + (double)sig->switches / sig->seconds) - 3.0) / 2.7);
	/* Packets on a log scale: 1/s is step 1, 1000/s step 10, 32000/s step 15 */
	out->net = stepper_feed(&m->net, log2(1.0 + (double)sig->packets / sig->seconds) / 15.0);
	out->disk = stepper_feed(&m->disk, sig->disk_busy);
	out->error = sig->log_errors > 0;
	out->inbound = sig->inbound > 0;
}

int syn_hum_steps_line(const syn_hum_steps *st, const char *style, double volume, char *out, size_t len) {
	int n = snprintf(out, len, "HUM style=%s volume=%.2f stall=%d net=%d disk=%d error=%d inbound=%d "
	                 "spawn=%d sched=%d cpuwait=%d iowait=%d cores=",
	                 style, volume, st->stall, st->net, st->disk, st->error, st->inbound,
	                 st->spawn, st->sched, st->cpu_wait, st->io_wait);
	for (int i = 0; i < st->core_count && n >= 0 && (size_t)n < len; i++) {
		n += snprintf(out + n, len - (size_t)n, i ? ",%d" : "%d", st->cores[i]);
	}
	if (n < 0 || (size_t)n >= len) return -1;
	return n;
}
