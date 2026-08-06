/* ------------------------------------------------------------------------
 *   Thin client for syn-bar-core's SYSMON-CPU/SYSMON-MEM verbs — lets
 *   syn-sysmon show a connected SYN-RELAY remote's stats instead of (or
 *   alongside) this machine's own, reusing whatever connection syn-relay
 *   already established rather than opening a second one.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SYSMON (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_BAR_CLIENT_H
#define SYN_BAR_CLIENT_H

#include <stdbool.h>

typedef enum {
	SYN_BAR_SOURCE_UNAVAILABLE, /* daemon unreachable, or verb returned {} */
	SYN_BAR_SOURCE_LOCAL,
	SYN_BAR_SOURCE_REMOTE,
	SYN_BAR_SOURCE_WAITING,     /* relay active but not yet connected */
} syn_bar_source;

typedef struct {
	syn_bar_source source;
	char host[256];            /* set when source == REMOTE */
	double total_pct;
	double load1, load5, load15; /* local only; 0 for remote */
	double cores[64];
	int core_count;             /* local only; 0 for remote */
} syn_bar_cpu_reply;

typedef struct {
	syn_bar_source source;
	char host[256];
	unsigned long long total_kb, free_kb, available_kb;
	unsigned long long buffers_kb, cached_kb;
	unsigned long long swap_total_kb, swap_free_kb; /* local only */
	unsigned long long used_kb;                      /* remote only (already computed) */
} syn_bar_mem_reply;

/* Queries syn-bar-core's SYSMON-CPU verb. Returns false (source left
 * UNAVAILABLE) if the daemon isn't running or the socket can't be
 * reached — caller should fall back to its own local reader. */
bool syn_bar_client_cpu(syn_bar_cpu_reply *out);

/* Same for SYSMON-MEM. */
bool syn_bar_client_mem(syn_bar_mem_reply *out);

#endif
