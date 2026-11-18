/* ------------------------------------------------------------------------
 *   The tree a scan builds: every folder and file under a root with the
 *   space it takes. Plain C, no graphics; the same on Linux and Windows.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_DIRSTAT_SCAN_H
#define SYN_DIRSTAT_SCAN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Node {
	struct Node *parent;
	struct Node **kids; /* largest first */
	int nkids;
	char *name;      /* one path part; the root's is the whole path */
	uint64_t size;   /* bytes, with everything under it */
	uint64_t files;  /* files under it (1 for a file) */
	int64_t mtime;   /* newest change under it, seconds since 1970 */
	bool is_dir;
	bool unread;     /* a folder that could not be opened */
	bool open;       /* shown expanded in the list */
} Node;

/* What a running scan has counted so far; read from the window's thread
 * while the scan's thread writes, so only plain counters. */
typedef struct {
	volatile uint64_t files, bytes;
	volatile bool cancel;
} ScanProgress;

/* Walks path into a tree. Symbolic links and Windows junctions are counted
 * as themselves and never followed, and the walk stays on the root's file
 * system. Returns NULL (with why in err) if path is not a folder that can
 * be read, or if the scan was cancelled. */
Node *scan_tree(const char *path, ScanProgress *progress, char *err, int errlen);
void scan_free(Node *root);

/* The node's whole path, parts joined by '/'. */
void node_path(const Node *n, char *buf, int len);

/* "1.4 GiB" */
void format_size(uint64_t bytes, char *buf, int len);

#endif
