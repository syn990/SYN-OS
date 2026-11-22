/* ------------------------------------------------------------------------
 *   The tree a scan builds: every folder and file under a root with the
 *   space it takes. Plain C, no graphics; the same on Linux and Windows.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SHELL_SPACE_SCAN_H
#define SYN_SHELL_SPACE_SCAN_H

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
	bool done;       /* counted to the end: a file, or a folder whose walk has finished */
	bool dirty;      /* something under it has grown since its children were last put in size order */
	int kids_cap;
} Node;

/* What a running scan has counted so far; read from the window's thread
 * while the scan's thread writes, so only plain counters.
 *
 * Given lock and unlock, the scan is LIVE: the tree is published in root
 * the moment it is started and grows in place, every folder and file
 * hung on it the moment it is found, and every folder's size, file count
 * and time counting up as what is under it is counted. The scan holds
 * the lock only while it changes the tree, so the window, holding it,
 * may read the tree as it stands. A live scan that is cancelled or fails
 * leaves its tree in root for the caller to free. */
typedef struct {
	volatile uint64_t files, bytes;
	volatile bool cancel;
	void (*lock)(void *arg);
	void (*unlock)(void *arg);
	void *lock_arg;
	Node *volatile root;
} ScanProgress;

/* Walks path into a tree. Symbolic links and Windows junctions are counted
 * as themselves and never followed, and the walk stays on the root's file
 * system. Returns NULL (with why in err) if path is not a folder that can
 * be read, or if the scan was cancelled. */
Node *scan_tree(const char *path, ScanProgress *progress, char *err, int errlen);
void scan_free(Node *root);

/* One thing on its own, for the tree to take in after it was made or
 * moved: a folder with everything under it, or a file. Its name is the
 * last part of the path and it has no parent yet. NULL, with why in err,
 * if it cannot be read. */
Node *scan_one(const char *path, char *err, int errlen);

/* The node's whole path, parts joined by '/'. */
void node_path(const Node *n, char *buf, int len);

/* "1.4 GiB" */
void format_size(uint64_t bytes, char *buf, int len);

#endif
