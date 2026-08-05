/* ------------------------------------------------------------------------
 *   Root-filesystem usage via statvfs(2) — ported from
 *   syn-bar-disk-src/src/main.c's root-mount reading, without that
 *   tool's multi-mount tooltip enumeration (the stats server role's
 *   STATS reply only needs root usage, matching what the bar shows for
 *   a local machine).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY-SERVER (Remote node)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_DISK_H
#define SYN_DISK_H

#include <stdbool.h>

typedef struct {
	unsigned long long used_kb;
	unsigned long long total_kb;
} syn_disk_snapshot;

/* Reads "/"'s usage into *out via statvfs(2). Returns false (and leaves
 * *out zeroed) if statvfs fails. */
bool syn_disk_read(syn_disk_snapshot *out);

#endif
