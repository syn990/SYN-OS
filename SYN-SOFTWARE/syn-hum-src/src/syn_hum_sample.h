/* ------------------------------------------------------------------------
 *   The signals syn-hum turns into sound, read straight from /proc: CPU
 *   (each core and all of them), memory, CPU and IO pressure, processes
 *   started and context switches, packets, disk IO, new inbound TCP
 *   connections and log errors. Levels are shares of the
 *   time since the last sample, counts are what happened since it.
 *   Opening the sampler takes the first readings, so the first sample
 *   already covers a real interval.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_HUM_SAMPLE_H
#define SYN_HUM_SAMPLE_H

#define SYN_HUM_MAX_CORES 64 /* syn_stats.h's SYN_STATS_MAX_CORES */

typedef struct {
	double seconds;                /* since the previous sample */
	double cpu;                    /* 0-1, all cores together (/proc/stat) */
	int core_count;
	double core[SYN_HUM_MAX_CORES]; /* 0-1, each core */
	double mem_stall;              /* 0-1, time some task waited for memory (/proc/pressure/memory), -1 without PSI */
	unsigned long long packets;    /* in and out, every interface but lo (/proc/net/dev) */
	double disk_busy;              /* 0-1, the busiest disk's time spent on IO (/proc/diskstats) */
	unsigned long long disk_bytes; /* read and written, every disk */
	int inbound;                   /* TCP connections from another machine that weren't there last time */
	int log_errors;                /* log lines at error priority or worse */
	unsigned long long spawns;     /* processes started (/proc/stat's processes) */
	unsigned long long switches;   /* context switches (/proc/stat's ctxt) */
	double cpu_wait;               /* 0-1, time some task waited for a CPU (/proc/pressure/cpu), -1 without PSI */
	double io_wait;                /* 0-1, time some task waited on IO (/proc/pressure/io), -1 without PSI */
} syn_hum_signals;

typedef struct syn_hum_sampler syn_hum_sampler;

/* NULL only when out of memory. A source that can't be read reads as
 * zero (memory pressure as -1); the two below say which ones those are. */
syn_hum_sampler *syn_hum_sampler_open(void);
void syn_hum_sampler_close(syn_hum_sampler *s);

int syn_hum_sampler_has_psi(const syn_hum_sampler *s);
int syn_hum_sampler_has_logs(const syn_hum_sampler *s);

void syn_hum_sample(syn_hum_sampler *s, syn_hum_signals *out);

#endif
