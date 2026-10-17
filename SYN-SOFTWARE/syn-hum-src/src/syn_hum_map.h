/* ------------------------------------------------------------------------
 *   Signals into steps: each level syn_hum_sample.c reads becomes a step
 *   0-15, the way a redstone signal has a strength, and the steps become
 *   the HUM line syn-bar-core plays (syn_bar_hum.h has the line). A level
 *   is averaged over about 0.4 s and a step holds until the level has
 *   clearly left it, so a core's 10% jumps at ten samples a second don't
 *   make the sound flicker.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_HUM_MAP_H
#define SYN_HUM_MAP_H

#include "syn_hum_sample.h"

#include <stddef.h>

#define SYN_HUM_MAP_CORES 16 /* the voice gives the first 16 cores a note each */

typedef struct {
	int cores[SYN_HUM_MAP_CORES];
	int core_count;
	int stall, net, disk; /* memory, network, disk */
	int error, inbound;   /* one-offs this sample */
	int spawn, sched;     /* processes started, context switches */
	int cpu_wait, io_wait; /* tasks waiting for a CPU, on IO */
} syn_hum_steps;

typedef struct {
	double level;
	int step;
} syn_hum_stepper;

typedef struct {
	syn_hum_stepper cores[SYN_HUM_MAP_CORES], stall, net, disk;
	syn_hum_stepper spawn, sched, cpu_wait, io_wait;
} syn_hum_map;

/* `m` starts zeroed and carries the averages from one sample to the next */
void syn_hum_map_signals(syn_hum_map *m, const syn_hum_signals *sig, syn_hum_steps *out);

/* The HUM line for these steps in this style and volume. Returns its
 * length, or -1 if `len` was too short. */
int syn_hum_steps_line(const syn_hum_steps *st, const char *style, double volume, char *out, size_t len);

#endif
