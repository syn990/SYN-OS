/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int64_t now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int64_t wall_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void logf_(const char *fmt, ...) {
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	char stamp[16];
	strftime(stamp, sizeof stamp, "%H:%M:%S", &tm);
	fprintf(stderr, "%s syn-cast: ", stamp);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *b64_encode(const uint8_t *in, size_t n, bool url) {
	size_t out_n = 4 * ((n + 2) / 3);
	char *out = malloc(out_n + 1);
	if (!out) {
		return NULL;
	}
	size_t o = 0;
	for (size_t i = 0; i < n; i += 3) {
		unsigned v = (unsigned)in[i] << 16;
		if (i + 1 < n) v |= (unsigned)in[i + 1] << 8;
		if (i + 2 < n) v |= in[i + 2];
		out[o++] = B64[(v >> 18) & 63];
		out[o++] = B64[(v >> 12) & 63];
		out[o++] = i + 1 < n ? B64[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < n ? B64[v & 63] : '=';
	}
	out[o] = 0;
	if (url) {
		for (size_t i = 0; i < o; i++) {
			if (out[i] == '+') out[i] = '-';
			else if (out[i] == '/') out[i] = '_';
		}
		while (o && out[o - 1] == '=') out[--o] = 0;
	}
	return out;
}

static int b64_val(char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+' || c == '-') return 62;
	if (c == '/' || c == '_') return 63;
	return -1;
}

long b64_decode(const char *in, uint8_t *out, size_t out_cap) {
	unsigned acc = 0;
	int bits = 0;
	size_t o = 0;
	for (; *in && *in != '='; in++) {
		int v = b64_val(*in);
		if (v < 0) {
			return -1;
		}
		acc = (acc << 6) | (unsigned)v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (o >= out_cap) {
				return -1;
			}
			out[o++] = (uint8_t)(acc >> bits);
		}
	}
	return (long)o;
}

size_t utf8_next(const char *s, size_t n, uint32_t *cp) {
	const unsigned char *u = (const unsigned char *)s;
	if (n == 0) {
		*cp = 0;
		return 0;
	}
	if (u[0] < 0x80) {
		*cp = u[0];
		return 1;
	}
	int len = (u[0] & 0xE0) == 0xC0 ? 2 : (u[0] & 0xF0) == 0xE0 ? 3 : (u[0] & 0xF8) == 0xF0 ? 4 : 0;
	if (len == 0 || (size_t)len > n) {
		*cp = 0xFFFD;
		return 1;
	}
	uint32_t c = u[0] & (0xFF >> (len + 1));
	for (int i = 1; i < len; i++) {
		if ((u[i] & 0xC0) != 0x80) {
			*cp = 0xFFFD;
			return 1;
		}
		c = (c << 6) | (u[i] & 0x3F);
	}
	*cp = c;
	return (size_t)len;
}

size_t utf8_prev_len(const char *s, size_t i) {
	if (i == 0) {
		return 0;
	}
	size_t k = i - 1;
	while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80 && i - k < 4) {
		k--;
	}
	return i - k;
}

char *expand_home(const char *path, char *out, size_t cap) {
	const char *home = getenv("HOME");
	if (path[0] == '~' && (path[1] == '/' || path[1] == 0) && home) {
		snprintf(out, cap, "%s%s", home, path + 1);
	} else {
		snprintf(out, cap, "%s", path);
	}
	return out;
}

int mkdir_p(const char *path, unsigned mode) {
	char buf[1024];
	snprintf(buf, sizeof buf, "%s", path);
	for (char *p = buf + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (mkdir(buf, mode) != 0 && errno != EEXIST) {
				return -1;
			}
			*p = '/';
		}
	}
	if (mkdir(buf, mode) != 0 && errno != EEXIST) {
		return -1;
	}
	return 0;
}

uint8_t *read_file(const char *path, size_t *len, size_t max) {
	FILE *f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	size_t cap = 65536, n = 0;
	uint8_t *buf = malloc(cap + 1);
	while (buf) {
		if (n == cap) {
			if (cap >= max) {
				free(buf);
				buf = NULL;
				break;
			}
			cap *= 2;
			uint8_t *nb = realloc(buf, cap + 1);
			if (!nb) {
				free(buf);
				buf = NULL;
				break;
			}
			buf = nb;
		}
		size_t r = fread(buf + n, 1, cap - n, f);
		if (r == 0) {
			break;
		}
		n += r;
	}
	fclose(f);
	if (buf) {
		buf[n] = 0;
		if (len) {
			*len = n;
		}
	}
	return buf;
}

int write_file(const char *path, const void *data, size_t len, unsigned mode) {
	char tmp[1100];
	snprintf(tmp, sizeof tmp, "%s.tmp%d", path, (int)getpid());
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
	if (fd < 0) {
		return -1;
	}
	const uint8_t *p = data;
	while (len) {
		ssize_t w = write(fd, p, len);
		if (w < 0) {
			if (errno == EINTR) continue;
			close(fd);
			unlink(tmp);
			return -1;
		}
		p += w;
		len -= (size_t)w;
	}
	if (close(fd) != 0 || rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

void str_copy(char *dst, size_t cap, const char *src) {
	if (cap == 0) {
		return;
	}
	size_t n = src ? strlen(src) : 0;
	if (n >= cap) {
		n = cap - 1;
		/* never cut a UTF-8 sequence in half */
		while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) {
			n--;
		}
	}
	if (n) {
		memcpy(dst, src, n);
	}
	dst[n] = 0;
}

void spawn_detached(char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) {
		return;
	}
	if (pid == 0) {
		if (fork() == 0) {
			int fd = open("/dev/null", O_RDWR);
			if (fd >= 0) {
				dup2(fd, 0);
				dup2(fd, 1);
				dup2(fd, 2);
			}
			setsid();
			execvp(argv[0], argv);
			_exit(127);
		}
		_exit(0);
	}
	waitpid(pid, NULL, 0);
}
