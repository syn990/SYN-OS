/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-DIRSTAT (Disk usage)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "scan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *must(void *p) {
	if (!p) {
		fprintf(stderr, "syn-dirstat: out of memory\n");
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

/* A folder's children while it is being read. */
typedef struct {
	Node **v;
	int n, cap;
} Kids;

static void kids_add(Kids *k, Node *n) {
	if (k->n == k->cap) {
		k->cap = k->cap ? k->cap * 2 : 16;
		k->v = must(realloc(k->v, (size_t)k->cap * sizeof *k->v));
	}
	k->v[k->n++] = n;
}

static int by_size_desc(const void *a, const void *b) {
	const Node *x = *(Node *const *)a, *y = *(Node *const *)b;
	if (x->size != y->size) {
		return x->size < y->size ? 1 : -1;
	}
	return strcmp(x->name, y->name);
}

/* The children read, the folder's totals follow from them. */
static void dir_finish(Node *dir, Kids *k) {
	qsort(k->v, (size_t)k->n, sizeof *k->v, by_size_desc);
	dir->kids = k->v;
	dir->nkids = k->n;
	for (int i = 0; i < k->n; i++) {
		dir->size += k->v[i]->size;
		dir->files += k->v[i]->files;
		if (k->v[i]->mtime > dir->mtime) {
			dir->mtime = k->v[i]->mtime;
		}
	}
}

static void file_count(Node *n, uint64_t size, int64_t mtime, ScanProgress *p) {
	n->size = size;
	n->files = 1;
	n->mtime = mtime;
	p->files++;
	p->bytes += size;
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
		dir->unread = true;
		return;
	}
	Kids kids = {0};
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
		kids_add(&kids, n);
		int64_t mtime = unix_time(fd.ftLastWriteTime);
		if (n->is_dir) {
			n->mtime = mtime;
			wpath_add(p, L"\\");
			wpath_add(p, fd.cFileName);
			scan_dir(n, p, prog);
			p->len = base;
			p->s[base] = L'\0';
		} else {
			uint64_t size = link ? 0 : ((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
			file_count(n, size, mtime, prog);
		}
	} while (FindNextFileW(h, &fd));
	FindClose(h);
	dir_finish(dir, &kids);
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
	scan_dir(root, &p, prog);
	free(p.s);
	if (prog->cancel || root->unread) {
		snprintf(err, (size_t)errlen, "%s: %s", path, prog->cancel ? "cancelled" : "cannot be read");
		scan_free(root);
		return NULL;
	}
	return root;
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
		dir->unread = true;
		return;
	}
	Kids kids = {0};
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
		kids_add(&kids, n);
		if (!is_dir) {
			file_count(n, (uint64_t)st.st_blocks * 512, st.st_mtime, prog);
			continue;
		}
		n->mtime = st.st_mtime;
		n->size = (uint64_t)st.st_blocks * 512;
		if (st.st_dev != dev) {
			continue; /* another file system mounted here */
		}
		int sub = openat(fd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
		if (sub < 0) {
			n->unread = true;
			continue;
		}
		scan_dir(n, sub, dev, prog);
	}
	closedir(d);
	dir_finish(dir, &kids);
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
	scan_dir(root, fd, st.st_dev, prog);
	if (prog->cancel || root->unread) {
		snprintf(err, (size_t)errlen, "%s: %s", path, prog->cancel ? "cancelled" : "cannot be read");
		scan_free(root);
		return NULL;
	}
	return root;
}
#endif
