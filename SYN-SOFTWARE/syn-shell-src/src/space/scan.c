/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *must(void *p) {
	if (!p) {
		fprintf(stderr, "syn-shell: out of memory\n");
		abort();
	}
	return p;
}

static Node *node_new(Node *parent, const char *name, bool is_dir) {
	Node *n = must(calloc(1, sizeof *n));
	n->parent = parent;
	n->name = must(strdup(name));
	n->is_dir = is_dir;
	return n;
}

static int by_size_desc(const void *a, const void *b) {
	const Node *x = *(Node *const *)a, *y = *(Node *const *)b;
	if (x->size != y->size) {
		return x->size < y->size ? 1 : -1;
	}
	return strcmp(x->name, y->name);
}

static void hold(ScanProgress *p) {
	if (p->lock) {
		p->lock(p->lock_arg);
	}
}

static void let_go(ScanProgress *p) {
	if (p->unlock) {
		p->unlock(p->lock_arg);
	}
}

/* Hangs n, its own size, file count and time already set, in dir: every
 * folder from dir up takes n's share in at once, so the totals on the way
 * up are always what has been counted so far. */
static void attach(Node *dir, Node *n, ScanProgress *p) {
	hold(p);
	if (dir->nkids == dir->kids_cap) {
		dir->kids_cap = dir->kids_cap ? dir->kids_cap * 2 : 16;
		dir->kids = must(realloc(dir->kids, (size_t)dir->kids_cap * sizeof *dir->kids));
	}
	dir->kids[dir->nkids++] = n;
	for (Node *d = dir; d; d = d->parent) {
		d->size += n->size;
		d->files += n->files;
		if (n->mtime > d->mtime) {
			d->mtime = n->mtime;
		}
		d->dirty = true;
	}
	if (!n->is_dir) {
		p->files++;
		p->bytes += n->size;
	}
	let_go(p);
}

/* The folder's walk has ended: its children in size order, and it done. */
static void dir_finish(Node *dir, ScanProgress *p) {
	hold(p);
	if (dir->nkids > 1) {
		qsort(dir->kids, (size_t)dir->nkids, sizeof *dir->kids, by_size_desc);
	}
	dir->done = true;
	let_go(p);
}

/* A folder that will not be walked: unreadable, or another file system. */
static void dir_skip(Node *dir, bool unread, ScanProgress *p) {
	hold(p);
	dir->unread = unread;
	dir->done = true;
	let_go(p);
}

/* The root is the tree's first node; a live scan publishes it at once. */
static void publish(Node *root, ScanProgress *p) {
	hold(p);
	p->root = root;
	let_go(p);
}

/* A scan that did not end with a tree: a live one leaves what it built to
 * its caller, who may be showing it. */
static void scan_drop(Node *root, ScanProgress *p) {
	if (!p->lock) {
		scan_free(root);
	}
}

void scan_free(Node *n) {
	for (int i = 0; i < n->nkids; i++) {
		scan_free(n->kids[i]);
	}
	free(n->kids);
	free(n->name);
	free(n);
}

void node_path(const Node *n, char *buf, int len) {
	if (!n->parent) {
		snprintf(buf, (size_t)len, "%s", n->name);
		return;
	}
	node_path(n->parent, buf, len);
	size_t at = strlen(buf);
	/* the root may be "/" or "C:/", which already end the part */
	const char *sep = at && buf[at - 1] == '/' ? "" : "/";
	snprintf(buf + at, (size_t)len - at, "%s%s", sep, n->name);
}

void format_size(uint64_t bytes, char *buf, int len) {
	static const char *UNIT[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
	double v = (double)bytes;
	int u = 0;
	while (v >= 1024.0 && u < 5) {
		v /= 1024.0;
		u++;
	}
	if (u == 0) {
		snprintf(buf, (size_t)len, "%llu B", (unsigned long long)bytes);
	} else {
		snprintf(buf, (size_t)len, "%.1f %s", v, UNIT[u]);
	}
}

#ifdef _WIN32
/* ---- Windows: wide names with the \\?\ prefix, so no path is too long ---- */
#include <windows.h>

typedef struct {
	wchar_t *s;
	size_t len, cap;
} WPath;

static void wpath_room(WPath *p, size_t extra) {
	if (p->len + extra + 1 > p->cap) {
		p->cap = (p->len + extra + 1) * 2;
		p->s = must(realloc(p->s, p->cap * sizeof *p->s));
	}
}

static void wpath_add(WPath *p, const wchar_t *part) {
	size_t n = wcslen(part);
	wpath_room(p, n);
	memcpy(p->s + p->len, part, (n + 1) * sizeof *part);
	p->len += n;
}

static int64_t unix_time(FILETIME ft) {
	uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	return (int64_t)(t / 10000000ULL) - 11644473600LL;
}

/* p names the folder, with no '\' at its end. */
static void scan_dir(Node *dir, WPath *p, ScanProgress *prog) {
	size_t base = p->len;
	wpath_add(p, L"\\*");
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileExW(p->s, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL,
	                            FIND_FIRST_EX_LARGE_FETCH);
	p->len = base;
	p->s[base] = L'\0';
	if (h == INVALID_HANDLE_VALUE) {
		dir_skip(dir, true, prog);
		return;
	}
	do {
		if (prog->cancel) {
			break;
		}
		if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) {
			continue;
		}
		char name[1024];
		WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, sizeof name, NULL, NULL);
		bool is_dir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
		bool link = fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT;
		Node *n = node_new(dir, name, is_dir && !link);
		n->mtime = unix_time(fd.ftLastWriteTime);
		if (!n->is_dir) {
			n->size = link ? 0 : ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
			n->files = 1;
			n->done = true;
		}
		attach(dir, n, prog);
		if (n->is_dir) {
			wpath_add(p, L"\\");
			wpath_add(p, fd.cFileName);
			scan_dir(n, p, prog);
			p->len = base;
			p->s[base] = L'\0';
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	dir_finish(dir, prog);
}

Node *scan_tree(const char *path, ScanProgress *prog, char *err, int errlen) {
	wchar_t given[4096], full[4096];
	if (!MultiByteToWideChar(CP_UTF8, 0, path, -1, given, 4096) ||
	    !GetFullPathNameW(given, 4096, full, NULL) || !full[0]) {
		snprintf(err, (size_t)errlen, "%s: not a path", path);
		return NULL;
	}
	size_t n = wcslen(full);
	while (n > 0 && full[n - 1] == L'\\') {
		full[--n] = L'\0';
	}
	/* "C:" alone means the folder one is in on C, so a drive is asked about as "C:\" */
	wchar_t probe[4100];
	swprintf(probe, 4100, L"%ls%ls", full, full[n - 1] == L':' ? L"\\" : L"");
	DWORD attr = GetFileAttributesW(probe);
	if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
		snprintf(err, (size_t)errlen, "%s: not a folder that can be read", path);
		return NULL;
	}
	/* \\server\share becomes \\?\UNC\server\share, C:\x becomes \\?\C:\x */
	WPath p = {0};
	if (full[0] == L'\\' && full[1] == L'\\') {
		wpath_add(&p, L"\\\\?\\UNC\\");
		wpath_add(&p, full + 2);
	} else {
		wpath_add(&p, L"\\\\?\\");
		wpath_add(&p, full);
	}
	char name[4096];
	WideCharToMultiByte(CP_UTF8, 0, full, -1, name, sizeof name, NULL, NULL);
	for (char *c = name; *c; c++) {
		if (*c == '\\') {
			*c = '/';
		}
	}
	if (name[strlen(name) - 1] == ':') {
		strcat(name, "/");
	}
	Node *root = node_new(NULL, name, true);
	publish(root, prog);
	scan_dir(root, &p, prog);
	free(p.s);
	if (prog->cancel || root->unread) {
		snprintf(err, (size_t)errlen, "%s: %s", path, prog->cancel ? "cancelled" : "cannot be read");
		scan_drop(root, prog);
		return NULL;
	}
	return root;
}

Node *scan_one(const char *path, char *err, int errlen) {
	(void)path;
	snprintf(err, (size_t)errlen, "not on Windows yet");
	return NULL;
}

#else
/* ---- Linux: openat and fstatat, the space taken from st_blocks ---- */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* fd is the open folder, and is closed here. */
static void scan_dir(Node *dir, int fd, dev_t dev, ScanProgress *prog) {
	DIR *d = fdopendir(fd);
	if (!d) {
		close(fd);
		dir_skip(dir, true, prog);
		return;
	}
	struct dirent *e;
	while (!prog->cancel && (e = readdir(d)) != NULL) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
			continue;
		}
		struct stat st;
		if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			continue;
		}
		bool is_dir = S_ISDIR(st.st_mode);
		Node *n = node_new(dir, e->d_name, is_dir);
		n->size = (uint64_t)st.st_blocks * 512;
		n->mtime = st.st_mtime;
		if (!is_dir) {
			n->files = 1;
			n->done = true;
		}
		attach(dir, n, prog);
		if (!is_dir) {
			continue;
		}
		if (st.st_dev != dev) {
			dir_skip(n, false, prog); /* another file system mounted here */
			continue;
		}
		int sub = openat(fd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (sub < 0) {
			dir_skip(n, true, prog);
			continue;
		}
		scan_dir(n, sub, dev, prog);
	}
	closedir(d);
	dir_finish(dir, prog);
}

Node *scan_tree(const char *path, ScanProgress *prog, char *err, int errlen) {
	char full[4096];
	struct stat st;
	if (!realpath(path, full) || stat(full, &st) != 0) {
		snprintf(err, (size_t)errlen, "%s: %s", path, strerror(errno));
		return NULL;
	}
	int fd = open(full, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) {
		snprintf(err, (size_t)errlen, "%s: %s", path, strerror(errno));
		return NULL;
	}
	Node *root = node_new(NULL, full, true);
	root->mtime = st.st_mtime;
	root->size = (uint64_t)st.st_blocks * 512;
	publish(root, prog);
	scan_dir(root, fd, st.st_dev, prog);
	if (prog->cancel || root->unread) {
		snprintf(err, (size_t)errlen, "%s: %s", path, prog->cancel ? "cancelled" : "cannot be read");
		scan_drop(root, prog);
		return NULL;
	}
	return root;
}

Node *scan_one(const char *path, char *err, int errlen) {
	struct stat st;
	if (lstat(path, &st) != 0) {
		snprintf(err, (size_t)errlen, "%s: %s", path, strerror(errno));
		return NULL;
	}
	const char *slash = strrchr(path, '/');
	const char *base = slash && slash[1] ? slash + 1 : path;
	if (!S_ISDIR(st.st_mode)) {
		Node *n = node_new(NULL, base, false);
		n->size = (uint64_t)st.st_blocks * 512;
		n->files = 1;
		n->mtime = st.st_mtime;
		n->done = true;
		return n;
	}
	ScanProgress prog = {0};
	Node *n = scan_tree(path, &prog, err, errlen);
	if (n) {
		free(n->name);
		n->name = must(strdup(base));
	}
	return n;
}
#endif
