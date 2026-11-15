/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (core)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _POSIX_C_SOURCE 200809L

#include "sn_wire.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool wait_fd(int fd, short events, int timeout_ms) {
	struct pollfd p = {.fd = fd, .events = events};
	for (;;) {
		int rc = poll(&p, 1, timeout_ms);
		if (rc < 0 && errno == EINTR) continue;
		return rc > 0;
	}
}

bool sn_write_all(int fd, const void *data, size_t len, int timeout_ms) {
	const uint8_t *p = data;
	while (len > 0) {
		if (!wait_fd(fd, POLLOUT, timeout_ms)) return false;
		ssize_t n = write(fd, p, len);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			return false;
		}
		p += n;
		len -= (size_t)n;
	}
	return true;
}

bool sn_read_exact(int fd, void *out, size_t len, int timeout_ms) {
	uint8_t *p = out;
	while (len > 0) {
		if (!wait_fd(fd, POLLIN, timeout_ms)) return false;
		ssize_t n = read(fd, p, len);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN) continue;
			return false;
		}
		if (n == 0) return false;
		p += n;
		len -= (size_t)n;
	}
	return true;
}

bool sn_send_frame(int fd, const void *data, size_t len, int timeout_ms) {
	uint8_t hdr[4];
	sn_put32(hdr, (uint32_t)len);
	return sn_write_all(fd, hdr, 4, timeout_ms) && sn_write_all(fd, data, len, timeout_ms);
}

bool sn_recv_frame(int fd, uint8_t **out, size_t *len, size_t max, int timeout_ms) {
	*out = NULL;
	uint8_t hdr[4];
	if (!sn_read_exact(fd, hdr, 4, timeout_ms)) return false;
	uint32_t n = sn_get32(hdr);
	if (n > max) return false;
	uint8_t *p = malloc(n ? n : 1);
	if (!p) return false;
	if (!sn_read_exact(fd, p, n, timeout_ms)) {
		free(p);
		return false;
	}
	*out = p;
	*len = n;
	return true;
}

long sn_frame_peek(const sn_buf *buf, size_t max, const uint8_t **frame, size_t *frame_len) {
	if (buf->len < 4) return 0;
	uint32_t n = sn_get32(buf->data);
	if (n > max) return -1;
	if (buf->len < 4 + (size_t)n) return 0;
	*frame = buf->data + 4;
	*frame_len = n;
	return 4 + (long)n;
}

bool sn_frame_append(sn_buf *buf, const void *data, size_t len) {
	uint8_t hdr[4];
	sn_put32(hdr, (uint32_t)len);
	return sn_buf_append(buf, hdr, 4) && sn_buf_append(buf, data, len);
}
