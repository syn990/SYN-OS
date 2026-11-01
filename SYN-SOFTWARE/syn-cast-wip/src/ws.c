/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "ws.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define CONNECT_TIMEOUT_MS 10000
#define MAX_MESSAGE (32u << 20)  /* a full-size image is a few MB */
#define MAX_BACKLOG (16u << 20)

struct ws {
	int fd;
	SSL_CTX *ctx;
	SSL *ssl;
	uint8_t *rx;
	size_t rx_len, rx_cap;
	uint8_t *tx;
	size_t tx_off, tx_len, tx_cap;
	uint8_t *frag;      /* a fragmented message being assembled */
	size_t frag_len, frag_cap;
	int frag_op;
	bool dead;
	int close_code;
	int64_t last_rx;
};

/* ---------- connect ---------- */

typedef struct {
	bool tls;
	char host[256];
	char port[8];
	char path[512];
} ws_url;

static int parse_url(const char *url, ws_url *u) {
	const char *p;
	if (strncmp(url, "wss://", 6) == 0) {
		u->tls = true;
		p = url + 6;
	} else if (strncmp(url, "ws://", 5) == 0) {
		u->tls = false;
		p = url + 5;
	} else {
		return -1;
	}
	const char *slash = strchr(p, '/');
	size_t hostport_n = slash ? (size_t)(slash - p) : strlen(p);
	char hostport[300];
	if (hostport_n == 0 || hostport_n >= sizeof hostport) {
		return -1;
	}
	memcpy(hostport, p, hostport_n);
	hostport[hostport_n] = 0;
	str_copy(u->path, sizeof u->path, slash ? slash : "/");
	char *colon = strrchr(hostport, ':');
	if (colon && !strchr(hostport, ']')) {
		*colon = 0;
		str_copy(u->port, sizeof u->port, colon + 1);
	} else {
		str_copy(u->port, sizeof u->port, u->tls ? "443" : "80");
	}
	str_copy(u->host, sizeof u->host, hostport);
	return 0;
}

static int tcp_connect(const char *host, const char *port, char *err, size_t errn) {
	struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM}, *res, *ai;
	int rc = getaddrinfo(host, port, &hints, &res);
	if (rc != 0) {
		snprintf(err, errn, "%s: %s", host, gai_strerror(rc));
		return -1;
	}
	int fd = -1;
	for (ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, ai->ai_protocol);
		if (fd < 0) {
			continue;
		}
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
			break;
		}
		if (errno == EINPROGRESS) {
			struct pollfd pfd = {.fd = fd, .events = POLLOUT};
			int soerr = 0;
			socklen_t sl = sizeof soerr;
			if (poll(&pfd, 1, CONNECT_TIMEOUT_MS) == 1 &&
				getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) == 0 && soerr == 0) {
				break;
			}
			errno = soerr ? soerr : ETIMEDOUT;
		}
		snprintf(err, errn, "%s:%s: %s", host, port, strerror(errno));
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	if (fd >= 0) {
		/* blocking with timeouts for the handshake; non-blocking after */
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
		struct timeval tv = {CONNECT_TIMEOUT_MS / 1000, 0};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
	}
	return fd;
}

static ssize_t raw_write(ws *w, const void *p, size_t n) {
	if (w->ssl) {
		int r = SSL_write(w->ssl, p, (int)n);
		return r > 0 ? r : -1;
	}
	return send(w->fd, p, n, MSG_NOSIGNAL);
}

static ssize_t raw_read(ws *w, void *p, size_t n) {
	if (w->ssl) {
		int r = SSL_read(w->ssl, p, (int)n);
		return r > 0 ? r : -1;
	}
	return recv(w->fd, p, n, 0);
}

static void ws_free(ws *w) {
	if (!w) {
		return;
	}
	if (w->ssl) {
		SSL_free(w->ssl);
	}
	if (w->ctx) {
		SSL_CTX_free(w->ctx);
	}
	if (w->fd >= 0) {
		close(w->fd);
	}
	free(w->rx);
	free(w->tx);
	free(w->frag);
	free(w);
}

static bool reserve(uint8_t **buf, size_t *cap, size_t need) {
	if (need <= *cap) {
		return true;
	}
	size_t n = *cap ? *cap : 16384;
	while (n < need) {
		n *= 2;
	}
	uint8_t *nb = realloc(*buf, n);
	if (!nb) {
		return false;
	}
	*buf = nb;
	*cap = n;
	return true;
}

ws *ws_connect(const char *url, const char *cert, char *err, size_t errn) {
	ws_url u;
	if (parse_url(url, &u) != 0) {
		snprintf(err, errn, "not a ws:// or wss:// URL: %s", url);
		return NULL;
	}
	ws *w = calloc(1, sizeof *w);
	w->fd = tcp_connect(u.host, u.port, err, errn);
	if (w->fd < 0) {
		ws_free(w);
		return NULL;
	}
	if (u.tls) {
		w->ctx = SSL_CTX_new(TLS_client_method());
		SSL_CTX_set_min_proto_version(w->ctx, TLS1_2_VERSION);
		bool pinned = cert && *cert;
		if (pinned) {
			/* the server's own certificate is the only trust anchor */
			if (SSL_CTX_load_verify_locations(w->ctx, cert, NULL) != 1) {
				snprintf(err, errn, "cannot read the pinned certificate %s", cert);
				ws_free(w);
				return NULL;
			}
		} else {
			SSL_CTX_set_default_verify_paths(w->ctx);
		}
		SSL_CTX_set_verify(w->ctx, SSL_VERIFY_PEER, NULL);
		w->ssl = SSL_new(w->ctx);
		SSL_set_tlsext_host_name(w->ssl, u.host);
		if (!pinned) {
			SSL_set1_host(w->ssl, u.host);
		}
		SSL_set_fd(w->ssl, w->fd);
		if (SSL_connect(w->ssl) != 1) {
			unsigned long e = ERR_get_error();
			snprintf(err, errn, "TLS to %s failed: %s", u.host,
				e ? ERR_reason_error_string(e) : "handshake");
			ws_free(w);
			return NULL;
		}
	}

	uint8_t keyraw[16];
	RAND_bytes(keyraw, sizeof keyraw);
	char *key = b64_encode(keyraw, sizeof keyraw, false);
	char req[1200];
	int reqn = snprintf(req, sizeof req,
		"GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nUser-Agent: syn-cast\r\n\r\n",
		u.path, u.host, key);
	if (raw_write(w, req, (size_t)reqn) != reqn) {
		snprintf(err, errn, "upgrade request to %s failed", u.host);
		free(key);
		ws_free(w);
		return NULL;
	}

	/* read the response headers; anything after them is already frames */
	char *end = NULL;
	while (!end) {
		if (!reserve(&w->rx, &w->rx_cap, w->rx_len + 4097)) {
			break;
		}
		ssize_t r = raw_read(w, w->rx + w->rx_len, 4096);
		if (r <= 0) {
			break;
		}
		w->rx_len += (size_t)r;
		w->rx[w->rx_len] = 0;
		end = strstr((char *)w->rx, "\r\n\r\n");
		if (!end && w->rx_len > 16384) {
			break;
		}
	}
	if (!end || strncmp((char *)w->rx, "HTTP/1.1 101", 12) != 0) {
		char line[120] = "no response";
		if (w->rx_len) {
			sscanf((char *)w->rx, "%119[^\r\n]", line);
		}
		snprintf(err, errn, "upgrade refused by %s: %s", u.host, line);
		free(key);
		ws_free(w);
		return NULL;
	}

	char accept_src[128];
	snprintf(accept_src, sizeof accept_src, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
	free(key);
	uint8_t sha[SHA_DIGEST_LENGTH];
	SHA1((const uint8_t *)accept_src, strlen(accept_src), sha);
	char *want = b64_encode(sha, sizeof sha, false);
	bool ok = false;
	for (char *line = strstr((char *)w->rx, "\r\n"); line && line < end; line = strstr(line + 2, "\r\n")) {
		if (strncasecmp(line + 2, "Sec-WebSocket-Accept:", 21) == 0) {
			char *v = line + 23;
			while (*v == ' ') v++;
			ok = strncmp(v, want, strlen(want)) == 0;
		}
	}
	free(want);
	if (!ok) {
		snprintf(err, errn, "upgrade from %s has a bad Sec-WebSocket-Accept", u.host);
		ws_free(w);
		return NULL;
	}
	size_t hdr_n = (size_t)(end + 4 - (char *)w->rx);
	memmove(w->rx, w->rx + hdr_n, w->rx_len - hdr_n);
	w->rx_len -= hdr_n;

	struct timeval none = {0, 0};
	setsockopt(w->fd, SOL_SOCKET, SO_RCVTIMEO, &none, sizeof none);
	setsockopt(w->fd, SOL_SOCKET, SO_SNDTIMEO, &none, sizeof none);
	fcntl(w->fd, F_SETFL, fcntl(w->fd, F_GETFL) | O_NONBLOCK);
	if (w->ssl) {
		SSL_set_mode(w->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
	}
	return w;
}

typedef struct {
	char url[512];
	char cert[1024];
	int notify_fd;
	uint64_t cookie;
} connect_job;

static void *connect_thread(void *arg) {
	connect_job *job = arg;
	ws_connect_result *res = calloc(1, sizeof *res);
	res->cookie = job->cookie;
	res->conn = ws_connect(job->url, job->cert, res->err, sizeof res->err);
	if (write(job->notify_fd, &res, sizeof res) != sizeof res) {
		ws_free(res->conn);
		free(res);
	}
	free(job);
	return NULL;
}

int ws_connect_async(const char *url, const char *cert, int notify_fd, uint64_t cookie) {
	connect_job *job = calloc(1, sizeof *job);
	str_copy(job->url, sizeof job->url, url);
	str_copy(job->cert, sizeof job->cert, cert ? cert : "");
	job->notify_fd = notify_fd;
	job->cookie = cookie;
	pthread_t t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	int rc = pthread_create(&t, &attr, connect_thread, job);
	pthread_attr_destroy(&attr);
	if (rc != 0) {
		free(job);
		return -1;
	}
	return 0;
}

/* ---------- I/O ---------- */

int ws_fd(const ws *w) {
	return w->fd;
}

int ws_close_code(const ws *w) {
	return w->close_code;
}

int64_t ws_last_rx(const ws *w) {
	return w->last_rx;
}

bool ws_wants_write(const ws *w) {
	return w->tx_off < w->tx_len;
}

size_t ws_backlog(const ws *w) {
	return w->tx_len - w->tx_off;
}

int ws_flush(ws *w) {
	if (w->dead) {
		return -1;
	}
	while (w->tx_off < w->tx_len) {
		size_t n = w->tx_len - w->tx_off;
		if (w->ssl) {
			int r = SSL_write(w->ssl, w->tx + w->tx_off, (int)(n > (1u << 30) ? (1u << 30) : n));
			if (r <= 0) {
				int e = SSL_get_error(w->ssl, r);
				if (e == SSL_ERROR_WANT_WRITE || e == SSL_ERROR_WANT_READ) {
					return 0;
				}
				w->dead = true;
				return -1;
			}
			w->tx_off += (size_t)r;
		} else {
			ssize_t r = send(w->fd, w->tx + w->tx_off, n, MSG_NOSIGNAL);
			if (r < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
					return 0;
				}
				w->dead = true;
				return -1;
			}
			w->tx_off += (size_t)r;
		}
	}
	w->tx_off = w->tx_len = 0;
	return 0;
}

int ws_send(ws *w, int opcode, const void *a, size_t an, const void *b, size_t bn) {
	if (w->dead) {
		return -1;
	}
	size_t len = an + bn;
	if (ws_backlog(w) + len > MAX_BACKLOG) {
		w->dead = true;
		return -1;
	}
	if (w->tx_off > 0 && w->tx_off == w->tx_len) {
		w->tx_off = w->tx_len = 0;
	}
	if (!reserve(&w->tx, &w->tx_cap, w->tx_len + len + 14)) {
		w->dead = true;
		return -1;
	}
	uint8_t *h = w->tx + w->tx_len;
	size_t hn = 0;
	h[hn++] = (uint8_t)(0x80 | opcode);
	if (len < 126) {
		h[hn++] = (uint8_t)(0x80 | len);
	} else if (len < 65536) {
		h[hn++] = 0x80 | 126;
		h[hn++] = (uint8_t)(len >> 8);
		h[hn++] = (uint8_t)len;
	} else {
		h[hn++] = 0x80 | 127;
		for (int i = 7; i >= 0; i--) {
			h[hn++] = (uint8_t)((uint64_t)len >> (8 * i));
		}
	}
	uint8_t mask[4];
	RAND_bytes(mask, 4);
	memcpy(h + hn, mask, 4);
	hn += 4;
	uint8_t *p = h + hn;
	if (an) memcpy(p, a, an);
	if (bn) memcpy(p + an, b, bn);
	for (size_t i = 0; i < len; i++) {
		p[i] ^= mask[i & 3];
	}
	w->tx_len += hn + len;
	return ws_flush(w);
}

static int handle_frame(ws *w, int fin, int op, uint8_t *data, size_t n, ws_frame_cb cb, void *ud) {
	switch (op) {
	case WS_PING:
		return ws_send(w, WS_PONG, data, n, NULL, 0);
	case WS_PONG:
		return 0;
	case WS_CLOSE:
		w->close_code = n >= 2 ? (data[0] << 8) | data[1] : 0;
		ws_send(w, WS_CLOSE, data, n > 2 ? 2 : n, NULL, 0);
		return -1;
	case 0: /* continuation */
		if (!w->frag_op || !reserve(&w->frag, &w->frag_cap, w->frag_len + n) ||
			w->frag_len + n > MAX_MESSAGE) {
			return -1;
		}
		memcpy(w->frag + w->frag_len, data, n);
		w->frag_len += n;
		if (fin) {
			int fop = w->frag_op;
			w->frag_op = 0;
			cb(ud, fop, w->frag, w->frag_len);
			w->frag_len = 0;
		}
		return 0;
	case WS_TEXT:
	case WS_BINARY:
		if (fin) {
			cb(ud, op, data, n);
			return 0;
		}
		if (!reserve(&w->frag, &w->frag_cap, n)) {
			return -1;
		}
		memcpy(w->frag, data, n);
		w->frag_len = n;
		w->frag_op = op;
		return 0;
	default:
		return -1;
	}
}

static int parse_frames(ws *w, ws_frame_cb cb, void *ud) {
	size_t off = 0;
	int rc = 0;
	while (rc == 0) {
		uint8_t *b = w->rx + off;
		size_t avail = w->rx_len - off;
		if (avail < 2) {
			break;
		}
		int fin = b[0] >> 7, op = b[0] & 0x0F;
		bool masked = b[1] >> 7;
		uint64_t len = b[1] & 0x7F;
		size_t hn = 2;
		if (len == 126) {
			if (avail < 4) break;
			len = ((uint64_t)b[2] << 8) | b[3];
			hn = 4;
		} else if (len == 127) {
			if (avail < 10) break;
			len = 0;
			for (int i = 0; i < 8; i++) {
				len = (len << 8) | b[2 + i];
			}
			hn = 10;
		}
		if (len > MAX_MESSAGE) {
			return -1;
		}
		if (masked) {
			hn += 4;
		}
		if (avail < hn + len) {
			break;
		}
		uint8_t *payload = b + hn;
		if (masked) {
			for (uint64_t i = 0; i < len; i++) {
				payload[i] ^= b[hn - 4 + (i & 3)];
			}
		}
		rc = handle_frame(w, fin, op, payload, (size_t)len, cb, ud);
		off += hn + (size_t)len;
	}
	if (off) {
		memmove(w->rx, w->rx + off, w->rx_len - off);
		w->rx_len -= off;
	}
	return rc;
}

int ws_read(ws *w, ws_frame_cb cb, void *ud) {
	if (w->dead) {
		return -1;
	}
	/* frames that arrived with the upgrade response */
	if (w->rx_len && parse_frames(w, cb, ud) != 0) {
		w->dead = true;
		return -1;
	}
	for (;;) {
		if (!reserve(&w->rx, &w->rx_cap, w->rx_len + 65536)) {
			w->dead = true;
			return -1;
		}
		ssize_t r;
		if (w->ssl) {
			int n = SSL_read(w->ssl, w->rx + w->rx_len, 65536);
			if (n <= 0) {
				int e = SSL_get_error(w->ssl, n);
				if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
					return 0;
				}
				w->dead = true;
				return -1;
			}
			r = n;
		} else {
			r = recv(w->fd, w->rx + w->rx_len, 65536, 0);
			if (r == 0) {
				w->dead = true;
				return -1;
			}
			if (r < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
					return 0;
				}
				w->dead = true;
				return -1;
			}
		}
		w->rx_len += (size_t)r;
		w->last_rx = now_ms();
		if (parse_frames(w, cb, ud) != 0) {
			w->dead = true;
			return -1;
		}
	}
}

void ws_close(ws *w) {
	if (!w) {
		return;
	}
	if (!w->dead) {
		uint8_t code[2] = {0x03, 0xE8}; /* 1000 normal */
		ws_send(w, WS_CLOSE, code, 2, NULL, 0);
		if (w->ssl) {
			SSL_shutdown(w->ssl);
		}
	}
	ws_free(w);
}
