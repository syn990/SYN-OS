/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "sn_util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

void sn_hex(const uint8_t *in, size_t len, char *out) {
	static const char digits[] = "0123456789abcdef";
	for (size_t i = 0; i < len; i++) {
		out[2 * i] = digits[in[i] >> 4];
		out[2 * i + 1] = digits[in[i] & 15];
	}
	out[2 * len] = '\0';
}

static int hexval(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool sn_unhex(const char *in, uint8_t *out, size_t len) {
	if (strlen(in) != 2 * len) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		int hi = hexval(in[2 * i]), lo = hexval(in[2 * i + 1]);
		if (hi < 0 || lo < 0) {
			return false;
		}
		out[i] = (uint8_t)(hi << 4 | lo);
	}
	return true;
}

void sn_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void sn_put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
void sn_put64(uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i)); }
uint16_t sn_get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t sn_get32(const uint8_t *p) {
	uint32_t v = 0;
	for (int i = 3; i >= 0; i--) v = v << 8 | p[i];
	return v;
}
uint64_t sn_get64(const uint8_t *p) {
	uint64_t v = 0;
	for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
	return v;
}

void sn_buf_free(sn_buf *b) {
	free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
}

bool sn_buf_append(sn_buf *b, const void *data, size_t len) {
	if (b->len + len > b->cap) {
		size_t cap = b->cap ? b->cap : 4096;
		while (cap < b->len + len) {
			cap *= 2;
		}
		uint8_t *p = realloc(b->data, cap);
		if (!p) {
			return false;
		}
		b->data = p;
		b->cap = cap;
	}
	memcpy(b->data + b->len, data, len);
	b->len += len;
	return true;
}

void sn_buf_consume(sn_buf *b, size_t n) {
	if (n >= b->len) {
		b->len = 0;
		return;
	}
	memmove(b->data, b->data + n, b->len - n);
	b->len -= n;
}

static bool mkdir_p(const char *path) {
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char *p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
				return false;
			}
			*p = '/';
		}
	}
	return mkdir(tmp, 0700) == 0 || errno == EEXIST;
}

bool sn_state_dir(char *out, size_t out_size) {
	const char *home = getenv("SN_HOME");
	if (home && home[0]) {
		snprintf(out, out_size, "%s", home);
	} else if ((home = getenv("XDG_DATA_HOME")) && home[0]) {
		snprintf(out, out_size, "%s/syn-node", home);
	} else if ((home = getenv("HOME")) && home[0]) {
		snprintf(out, out_size, "%s/.local/share/syn-node", home);
	} else {
		return false;
	}
	return mkdir_p(out);
}

bool sn_state_path(const char *name, char *out, size_t out_size) {
	char dir[400];
	if (!sn_state_dir(dir, sizeof(dir))) {
		return false;
	}
	snprintf(out, out_size, "%s/%s", dir, name);
	return true;
}

bool sn_write_file(const char *path, const void *data, size_t len, unsigned mode) {
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)mode);
	if (fd < 0) {
		return false;
	}
	fchmod(fd, (mode_t)mode);
	const uint8_t *p = data;
	size_t left = len;
	while (left > 0) {
		ssize_t n = write(fd, p, left);
		if (n < 0) {
			if (errno == EINTR) continue;
			close(fd);
			unlink(tmp);
			return false;
		}
		p += n;
		left -= (size_t)n;
	}
	if (fsync(fd) != 0 || close(fd) != 0) {
		unlink(tmp);
		return false;
	}
	return rename(tmp, path) == 0;
}

bool sn_read_file(const char *path, uint8_t **out, size_t *len, size_t cap) {
	*out = NULL;
	*len = 0;
	int fd = open(path, O_RDONLY);
	if (fd < 0) {
		return false;
	}
	sn_buf b = {0};
	uint8_t chunk[8192];
	for (;;) {
		ssize_t n = read(fd, chunk, sizeof(chunk));
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) break;
		if (b.len + (size_t)n > cap || !sn_buf_append(&b, chunk, (size_t)n)) {
			sn_buf_free(&b);
			close(fd);
			return false;
		}
	}
	close(fd);
	*out = b.data;
	*len = b.len;
	if (!*out) {
		*out = malloc(1); /* empty file: still a valid pointer to free */
	}
	return *out != NULL;
}

void sn_ctl_path(char *out, size_t out_size) {
	const char *s = getenv("SN_SOCK");
	if (s && s[0]) {
		snprintf(out, out_size, "%s", s);
		return;
	}
	const char *rt = getenv("XDG_RUNTIME_DIR");
	snprintf(out, out_size, "%s/syn-node.sock", rt && rt[0] ? rt : "/tmp");
}

uint16_t sn_port(void) {
	const char *s = getenv("SN_PORT");
	if (s && s[0]) {
		long v = strtol(s, NULL, 10);
		if (v > 0 && v < 65536) {
			return (uint16_t)v;
		}
	}
	return SN_DEFAULT_PORT;
}

uint64_t sn_now_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

uint64_t sn_unix_time(void) {
	return (uint64_t)time(NULL);
}

void sn_log(const char *fmt, ...) {
	char stamp[32];
	time_t t = time(NULL);
	struct tm tm;
	localtime_r(&t, &tm);
	strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm);
	fprintf(stderr, "%s syn-node: ", stamp);
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

void sn_name_field(const char *in, char out[32]) {
	memset(out, 0, 32);
	size_t j = 0;
	for (size_t i = 0; in[i] && j < 31; i++) {
		char c = in[i];
		if (c < 0x21 || c > 0x7e || c == '|') {
			c = '-';
		}
		out[j++] = c;
	}
}
