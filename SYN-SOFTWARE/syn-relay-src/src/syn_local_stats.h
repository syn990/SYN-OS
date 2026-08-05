/* ------------------------------------------------------------------------
 *   Local /proc reader for the three numbers waybar's cpu/memory/disk
 *   modules need — this is the ONLY local stats path in the stats
 *   client role (cmd_stats_client.c), used whenever there's no relay
 *   connection (the ABSENT and ACTIVE states from syn_node_state.h).
 *   Deliberately NOT a copy of the stats server role's fuller
 *   syn_stats.c (multi-core breakdown, hwmon sensors) — this only
 *   needs aggregate CPU%, mem used/total, and disk used/total, the
 *   same three numbers the stock waybar cpu/memory/custom-disk modules
 *   already showed before this feature existed. Keeps the stats client
 *   role usable with zero relay setup at all, matching every existing
 *   waybar module's self-contained design.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_LOCAL_STATS_H
#define SYN_LOCAL_STATS_H

/* Aggregate CPU usage percentage since the last call in this process
 * (two /proc/stat reads 150ms apart, same delta-based approach as
 * syn_stats.c). First call in a process has nothing to diff against, so
 * it always returns 0.0 — fine here since each invocation is a fresh
 * one-shot process anyway. */
double syn_local_cpu_pct(void);

/* /proc/loadavg's three load-average fields, for the tooltip. */
void syn_local_load_avg(double *one, double *five, double *fifteen);

/* /proc/meminfo-derived used/total in kB (used = total - available, same
 * definition waybar's own stock memory module uses). */
void syn_local_mem_kb(unsigned long long *used_kb, unsigned long long *total_kb);

/* statvfs("/")-derived used/total in kB. */
void syn_local_disk_kb(unsigned long long *used_kb, unsigned long long *total_kb);

#endif
