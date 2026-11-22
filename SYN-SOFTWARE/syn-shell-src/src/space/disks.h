/* ------------------------------------------------------------------------
 *   The disks and network shares the machine has mounted, with how full
 *   each is: the first screen's list.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SHELL_SPACE_DISKS_H
#define SYN_SHELL_SPACE_DISKS_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	char path[512];  /* where it is mounted: "/home", "C:/" */
	char label[128]; /* what it is: the device and file system, or the volume's name */
	uint64_t total, free;
	bool remote;     /* a network share */
} Disk;

/* Fills out with up to max disks and returns how many. */
int disks_list(Disk *out, int max);

#endif
