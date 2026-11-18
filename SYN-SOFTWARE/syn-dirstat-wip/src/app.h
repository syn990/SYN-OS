/* ------------------------------------------------------------------------
 *   The window: the disks to pick from, the scan's progress, then the
 *   list beside the view (blocks or graph, flat or solid).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_DIRSTAT_APP_H
#define SYN_DIRSTAT_APP_H

#include <stdbool.h>

typedef struct {
	const char *path;  /* what to scan at once; NULL to start at the disks */
	const char *theme; /* a theme's name; NULL for the system's */
	bool graph;        /* start in the node graph, not the blocks */
	bool solid;        /* start in 3D */
	const char *shot;  /* write the first finished picture here (PPM) and leave */
} AppOptions;

/* Runs the window until it is closed; the program's exit status. */
int app_run(const AppOptions *opt);

#endif
