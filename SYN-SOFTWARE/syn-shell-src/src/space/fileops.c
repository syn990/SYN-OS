/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-SHELL (Disk space)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* renameat2 */
#endif
#include "fileops.h"

#include <stdio.h>
#include <string.h>

bool fo_name_ok(const char *name, char *err, int errlen) {
	if (!name[0] || !strcmp(name, ".") || !strcmp(name, "..")) {
		snprintf(err, (size_t)errlen, "that is not a name");
		return false;
	}
	if (strchr(name, '/')) {
		snprintf(err, (size_t)errlen, "a name cannot have / in it");
		return false;
	}
	if (strlen(name) > 255) {
		snprintf(err, (size_t)errlen, "the name is too long");
		return false;
	}
	return true;
}

#ifdef _WIN32

static bool not_yet(char *err, int errlen) {
	snprintf(err, (size_t)errlen, "not on Windows yet");
	return false;
}
bool fo_rename(const char *p, const char *n, char *o, int ol, char *err, int el) {
	(void)p, (void)n, (void)o, (void)ol;
	return not_yet(err, el);
}
bool fo_mkdir(const char *d, const char *n, char *o, int ol, char *err, int el) {
	(void)d, (void)n, (void)o, (void)ol;
	return not_yet(err, el);
}
bool fo_paste(const char *s, const char *d, bool m, char *o, int ol, char *err, int el) {
	(void)s, (void)d, (void)m, (void)o, (void)ol;
	return not_yet(err, el);
}
bool fo_trash(const char *p, char *err, int el) {
	(void)p;
	return not_yet(err, el);
}
bool fo_remove(const char *p, char *err, int el) {
	(void)p;
	return not_yet(err, el);
}

#else

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE 1
#endif

static void say_errno(char *err, int errlen, const char *what) {
	snprintf(err, (size_t)errlen, "%s: %s", what, strerror(errno));
}

static bool exists(const char *path) {
	struct stat st;
	return lstat(path, &st) == 0;
}

/* The folder a path is in and its last part, without following a link
 * at the end: "/a/b/c" is "/a/b" and "c". */
static bool split(const char *path, char *dir, size_t dirlen, const char **base) {
	const char *slash = strrchr(path, '/');
	if (!slash || !slash[1]) {
		return false;
	}
	*base = slash + 1;
	size_t n = slash == path ? 1 : (size_t)(slash - path);
	if (n >= dirlen) {
		return false;
	}
	memcpy(dir, path, n);
	dir[n] = '\0';
	return true;
}

static void join(char *out, int outlen, const char *dir, const char *name) {
	size_t n = strlen(dir);
	snprintf(out, (size_t)outlen, "%s%s%s", dir, n && dir[n - 1] == '/' ? "" : "/", name);
}

bool fo_rename(const char *path, const char *name, char *out, int outlen, char *err, int errlen) {
	char dir[PATH_MAX];
	const char *base;
	if (!fo_name_ok(name, err, errlen)) {
		return false;
	}
	if (!split(path, dir, sizeof dir, &base)) {
		snprintf(err, (size_t)errlen, "%s cannot be renamed", path);
		return false;
	}
	join(out, outlen, dir, name);
	if (!strcmp(base, name)) {
		return true;
	}
	if (renameat2(AT_FDCWD, path, AT_FDCWD, out, RENAME_NOREPLACE) != 0) {
		if (errno == EEXIST) {
			snprintf(err, (size_t)errlen, "%s is already there", name);
		} else {
			say_errno(err, errlen, "rename");
		}
		return false;
	}
	return true;
}

bool fo_mkdir(const char *dir, const char *name, char *out, int outlen, char *err, int errlen) {
	if (!fo_name_ok(name, err, errlen)) {
		return false;
	}
	join(out, outlen, dir, name);
	if (mkdir(out, 0777) != 0) {
		if (errno == EEXIST) {
			snprintf(err, (size_t)errlen, "%s is already there", name);
		} else {
			say_errno(err, errlen, "new folder");
		}
		return false;
	}
	return true;
}

/* Runs a program to its end with what it says on stderr kept for err. */
static bool run(char *const argv[], char *err, int errlen) {
	int pipefd[2];
	if (pipe(pipefd) != 0) {
		say_errno(err, errlen, argv[0]);
		return false;
	}
	pid_t pid = fork();
	if (pid < 0) {
		say_errno(err, errlen, argv[0]);
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}
	if (pid == 0) {
		dup2(pipefd[1], 2);
		close(pipefd[0]);
		close(pipefd[1]);
		execvp(argv[0], argv);
		fprintf(stderr, "%s: %s", argv[0], strerror(errno));
		_exit(127);
	}
	close(pipefd[1]);
	char said[1024];
	size_t got = 0;
	ssize_t r;
	while ((r = read(pipefd[0], said + got, sizeof said - 1 - got)) > 0) {
		got += (size_t)r;
		if (got == sizeof said - 1) {
			char sink[256];
			while (read(pipefd[0], sink, sizeof sink) > 0) {
			}
			break;
		}
	}
	said[got] = '\0';
	close(pipefd[0]);
	int status;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		return true;
	}
	said[strcspn(said, "\n")] = '\0';
	snprintf(err, (size_t)errlen, "%s", said[0] ? said : "it failed");
	return false;
}

bool fo_paste(const char *src, const char *dir, bool move, char *out, int outlen, char *err, int errlen) {
	char from_dir[PATH_MAX];
	const char *base;
	if (!split(src, from_dir, sizeof from_dir, &base)) {
		snprintf(err, (size_t)errlen, "%s cannot be pasted", src);
		return false;
	}
	if (!exists(src)) {
		snprintf(err, (size_t)errlen, "%s is not there any more", src);
		return false;
	}
	/* a folder into itself or below itself never ends */
	char real_src[PATH_MAX], real_dir[PATH_MAX];
	if (realpath(src, real_src) && realpath(dir, real_dir)) {
		size_t n = strlen(real_src);
		if (!strncmp(real_dir, real_src, n) && (real_dir[n] == '/' || !real_dir[n])) {
			snprintf(err, (size_t)errlen, "a folder cannot go inside itself");
			return false;
		}
	}
	/* a free name: NAME, NAME (copy), NAME (copy 2) ... before any ending */
	const char *dot = strrchr(base, '.');
	size_t stem = dot && dot != base ? (size_t)(dot - base) : strlen(base);
	join(out, outlen, dir, base);
	for (int i = 1; exists(out); i++) {
		char name[NAME_MAX + 32];
		if (i == 1) {
			snprintf(name, sizeof name, "%.*s (copy)%s", (int)stem, base, base + stem);
		} else {
			snprintf(name, sizeof name, "%.*s (copy %d)%s", (int)stem, base, i, base + stem);
		}
		join(out, outlen, dir, name);
		if (i > 9999) {
			snprintf(err, (size_t)errlen, "no free name in %s", dir);
			return false;
		}
	}
	if (move) {
		if (renameat2(AT_FDCWD, src, AT_FDCWD, out, RENAME_NOREPLACE) == 0) {
			return true;
		}
		if (errno != EXDEV) {
			say_errno(err, errlen, "move");
			return false;
		}
		char *mv[] = {"mv", "-n", "-T", "--", (char *)src, out, NULL};
		return run(mv, err, errlen);
	}
	char *cp[] = {"cp", "-a", "-T", "--", (char *)src, out, NULL};
	return run(cp, err, errlen);
}

/* ---- the Trash, as freedesktop.org's Trash specification has it ---- */

static bool make_dir(const char *path) { return mkdir(path, 0700) == 0 || errno == EEXIST; }

/* The trash folder that suits a file on device dev, its files/ and info/
 * made, and in top where the file's Path= is counted from ("" for an
 * absolute Path=). */
static bool trash_for(dev_t dev, const char *abs, char *trash, size_t tlen, char *top, size_t toplen, char *err,
                      int errlen) {
	char data[PATH_MAX], sub[PATH_MAX + 16];
	const char *xdg = getenv("XDG_DATA_HOME"), *home = getenv("HOME");
	if (xdg && xdg[0] == '/') {
		snprintf(data, sizeof data, "%s", xdg);
	} else if (home && home[0] == '/') {
		snprintf(data, sizeof data, "%s/.local/share", home);
	} else {
		data[0] = '\0';
	}
	struct stat st;
	if (data[0] && stat(data, &st) == 0 && st.st_dev == dev) {
		snprintf(trash, tlen, "%s/Trash", data);
		top[0] = '\0';
	} else {
		/* the top of the file's own file system: up until the device changes */
		char cur[PATH_MAX];
		snprintf(cur, sizeof cur, "%s", abs);
		char *slash = strrchr(cur, '/');
		*(slash == cur ? slash + 1 : slash) = '\0';
		while (strcmp(cur, "/")) {
			char up[PATH_MAX];
			snprintf(up, sizeof up, "%s", cur);
			char *s = strrchr(up, '/');
			*(s == up ? s + 1 : s) = '\0';
			if (stat(up, &st) != 0 || st.st_dev != dev) {
				break;
			}
			snprintf(cur, sizeof cur, "%s", up);
		}
		snprintf(top, toplen, "%s", cur);
		snprintf(trash, tlen, "%s%s.Trash-%u", cur, strcmp(cur, "/") ? "/" : "", (unsigned)getuid());
		if (!make_dir(trash)) {
			say_errno(err, errlen, trash);
			return false;
		}
		/* the spec's checks: a real folder of one's own, not a link to elsewhere */
		if (lstat(trash, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()) {
			snprintf(err, (size_t)errlen, "%s is not a trash folder of yours", trash);
			return false;
		}
	}
	snprintf(sub, sizeof sub, "%s", trash);
	if (!top[0]) {
		/* ~/.local/share may not be there yet */
		for (char *p = sub + 1; *p; p++) {
			if (*p == '/') {
				*p = '\0';
				mkdir(sub, 0700);
				*p = '/';
			}
		}
		if (!make_dir(sub)) {
			say_errno(err, errlen, sub);
			return false;
		}
	}
	snprintf(sub, sizeof sub, "%s/files", trash);
	if (!make_dir(sub)) {
		say_errno(err, errlen, sub);
		return false;
	}
	snprintf(sub, sizeof sub, "%s/info", trash);
	if (!make_dir(sub)) {
		say_errno(err, errlen, sub);
		return false;
	}
	return true;
}

/* A path for Path=: every byte but the plain ones and '/' as %XX */
static void url_escape(const char *s, char *out, size_t outlen) {
	static const char HEX[] = "0123456789ABCDEF";
	size_t o = 0;
	for (const unsigned char *p = (const unsigned char *)s; *p && o + 4 < outlen; p++) {
		if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || strchr("-._~/", *p)) {
			out[o++] = (char)*p;
		} else {
			out[o++] = '%';
			out[o++] = HEX[*p >> 4];
			out[o++] = HEX[*p & 15];
		}
	}
	out[o] = '\0';
}

bool fo_trash(const char *path, char *err, int errlen) {
	char dir[PATH_MAX], real_dir[PATH_MAX], abs[PATH_MAX];
	const char *base;
	struct stat st;
	if (!split(path, dir, sizeof dir, &base) || lstat(path, &st) != 0) {
		snprintf(err, (size_t)errlen, "%s is not there", path);
		return false;
	}
	/* the folder it is in made real, the thing itself left as it is (a link stays a link) */
	if (!realpath(dir, real_dir)) {
		say_errno(err, errlen, dir);
		return false;
	}
	join(abs, sizeof abs, real_dir, base);
	char trash[PATH_MAX], top[PATH_MAX];
	if (!trash_for(st.st_dev, abs, trash, sizeof trash, top, sizeof top, err, errlen)) {
		return false;
	}
	const char *shown = abs;
	if (top[0]) {
		size_t n = strlen(top);
		shown = abs + n + (strcmp(top, "/") ? 1 : 0);
	}
	char esc[PATH_MAX * 3], when[32];
	url_escape(shown, esc, sizeof esc);
	time_t now = time(NULL);
	strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%S", localtime(&now));

	/* the info file is made first, with O_EXCL, which keeps the name for us */
	for (int i = 1; i < 10000; i++) {
		char name[NAME_MAX + 16], info[PATH_MAX + 300], dest[PATH_MAX + 300];
		if (i == 1) {
			snprintf(name, sizeof name, "%s", base);
		} else {
			snprintf(name, sizeof name, "%s.%d", base, i);
		}
		snprintf(info, sizeof info, "%s/info/%s.trashinfo", trash, name);
		snprintf(dest, sizeof dest, "%s/files/%s", trash, name);
		int fd = open(info, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
		if (fd < 0) {
			if (errno == EEXIST) {
				continue;
			}
			say_errno(err, errlen, info);
			return false;
		}
		if (exists(dest)) {
			close(fd);
			unlink(info);
			continue;
		}
		char text[PATH_MAX * 3 + 100];
		int len = snprintf(text, sizeof text, "[Trash Info]\nPath=%s\nDeletionDate=%s\n", esc, when);
		bool wrote = write(fd, text, (size_t)len) == len;
		close(fd);
		if (!wrote) {
			say_errno(err, errlen, info);
			unlink(info);
			return false;
		}
		if (renameat2(AT_FDCWD, abs, AT_FDCWD, dest, RENAME_NOREPLACE) != 0) {
			say_errno(err, errlen, "move to Trash");
			unlink(info);
			return false;
		}
		return true;
	}
	snprintf(err, (size_t)errlen, "no free name in %s", trash);
	return false;
}

/* ---- Delete permanently ---- */

/* Empties the open folder fd (on file system dev) and closes it; path is
 * its name, for what is said when something will not go. */
static bool remove_inside(int fd, dev_t dev, const char *path, char *err, int errlen) {
	DIR *d = fdopendir(fd);
	if (!d) {
		close(fd);
		say_errno(err, errlen, path);
		return false;
	}
	struct dirent *e;
	bool ok = true;
	while (ok && (errno = 0, e = readdir(d)) != NULL) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
			continue;
		}
		char sub[PATH_MAX];
		join(sub, sizeof sub, path, e->d_name);
		struct stat st;
		if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			say_errno(err, errlen, sub);
			ok = false;
		} else if (S_ISDIR(st.st_mode)) {
			if (st.st_dev != dev) {
				snprintf(err, (size_t)errlen, "%s is another disk mounted here; left alone", sub);
				ok = false;
				break;
			}
			int sfd = openat(fd, e->d_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
			if (sfd < 0) {
				say_errno(err, errlen, sub);
				ok = false;
			} else if (!remove_inside(sfd, dev, sub, err, errlen)) {
				ok = false;
			} else if (unlinkat(fd, e->d_name, AT_REMOVEDIR) != 0) {
				say_errno(err, errlen, sub);
				ok = false;
			}
		} else if (unlinkat(fd, e->d_name, 0) != 0) {
			say_errno(err, errlen, sub);
			ok = false;
		}
	}
	if (ok && errno) {
		say_errno(err, errlen, path);
		ok = false;
	}
	closedir(d);
	return ok;
}

bool fo_remove(const char *path, char *err, int errlen) {
	struct stat st;
	if (lstat(path, &st) != 0) {
		snprintf(err, (size_t)errlen, "%s is not there", path);
		return false;
	}
	if (!S_ISDIR(st.st_mode)) {
		if (unlink(path) != 0) {
			say_errno(err, errlen, path);
			return false;
		}
		return true;
	}
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0) {
		say_errno(err, errlen, path);
		return false;
	}
	if (!remove_inside(fd, st.st_dev, path, err, errlen)) {
		return false;
	}
	if (rmdir(path) != 0) {
		say_errno(err, errlen, path);
		return false;
	}
	return true;
}

#endif
