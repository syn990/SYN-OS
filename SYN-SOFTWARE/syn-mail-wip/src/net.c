/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-MAIL
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
/* The transport: tcp_connect and the TLS setup come from syn-cast's ws.c (TLS 1.2 at least, the system's CA store,
 * the host name checked). SYN_MAIL_CAFILE replaces the CA store, for the stand-in server in tests. */
#include "net.h"

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define CONNECT_TIMEOUT_MS 15000
#define IO_TIMEOUT_S 60
#define LINE_MAX_BYTES (1 << 20)

int net_trace = 0;

struct Net {
	int fd;
	SSL_CTX *ctx;
	SSL *ssl;
	char host[256];
	char rbuf[16384];
	size_t rpos, rlen;
};

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
		err[0] = 0; /* an earlier address may have failed (::1 before 127.0.0.1); that error is not this one's */
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
		struct timeval tv = {IO_TIMEOUT_S, 0};
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
	}
	return fd;
}

int net_starttls(Net *c, char *err, size_t errn) {
	c->ctx = SSL_CTX_new(TLS_client_method());
	SSL_CTX_set_min_proto_version(c->ctx, TLS1_2_VERSION);
	const char *ca = getenv("SYN_MAIL_CAFILE");
	if (ca && *ca) {
		if (SSL_CTX_load_verify_locations(c->ctx, ca, NULL) != 1) {
			snprintf(err, errn, "cannot read CA file %s", ca);
			return -1;
		}
	} else {
		SSL_CTX_set_default_verify_paths(c->ctx);
	}
	SSL_CTX_set_verify(c->ctx, SSL_VERIFY_PEER, NULL);
	c->ssl = SSL_new(c->ctx);
	SSL_set_tlsext_host_name(c->ssl, c->host);
	SSL_set1_host(c->ssl, c->host);
	SSL_set_fd(c->ssl, c->fd);
	if (SSL_connect(c->ssl) != 1) {
		unsigned long e = ERR_get_error();
		long v = SSL_get_verify_result(c->ssl);
		snprintf(err, errn, "TLS to %s failed: %s", c->host,
			v != X509_V_OK ? X509_verify_cert_error_string(v) : e ? ERR_reason_error_string(e) : "handshake");
		return -1;
	}
	c->rpos = c->rlen = 0;
	return 0;
}

Net *net_open(const char *host, int port, int tls, char *err, size_t errn) {
	char ps[16];
	snprintf(ps, sizeof ps, "%d", port);
	Net *c = calloc(1, sizeof *c);
	str_copy(c->host, sizeof c->host, host);
	c->fd = tcp_connect(host, ps, err, errn);
	if (c->fd < 0) {
		free(c);
		return NULL;
	}
	if (tls && net_starttls(c, err, errn) != 0) {
		net_close(c);
		return NULL;
	}
	return c;
}

int net_write(Net *c, const void *p, size_t n) {
	const char *q = p;
	while (n > 0) {
		ssize_t w = c->ssl ? SSL_write(c->ssl, q, (int)n) : write(c->fd, q, n);
		if (w <= 0) {
			if (!c->ssl && w < 0 && errno == EINTR) {
				continue;
			}
			return -1;
		}
		q += w;
		n -= (size_t)w;
	}
	return 0;
}

int net_puts(Net *c, const char *s) {
	if (net_trace) {
		if (str_ieq_prefix(s, "AUTH") || strstr(s, " AUTHENTICATE ") || strstr(s, " LOGIN ")) {
			fprintf(stderr, "> (sign-in line not shown)\n");
		} else {
			fprintf(stderr, "> %.*s\n", (int)strcspn(s, "\r\n"), s);
		}
	}
	return net_write(c, s, strlen(s));
}

static int fill(Net *c) {
	ssize_t r = c->ssl ? SSL_read(c->ssl, c->rbuf, sizeof c->rbuf) : read(c->fd, c->rbuf, sizeof c->rbuf);
	if (r <= 0) {
		return -1;
	}
	c->rpos = 0;
	c->rlen = (size_t)r;
	return 0;
}

int net_line(Net *c, Buf *out) {
	out->n = 0;
	if (out->p) {
		out->p[0] = 0;
	}
	for (;;) {
		if (c->rpos >= c->rlen && fill(c) != 0) {
			return -1;
		}
		char *s = c->rbuf + c->rpos;
		char *nl = memchr(s, '\n', c->rlen - c->rpos);
		size_t take = nl ? (size_t)(nl - s) + 1 : c->rlen - c->rpos;
		buf_put(out, s, take);
		c->rpos += take;
		if (out->n > LINE_MAX_BYTES) {
			return -1;
		}
		if (nl) {
			break;
		}
	}
	while (out->n && (out->p[out->n - 1] == '\n' || out->p[out->n - 1] == '\r')) {
		out->p[--out->n] = 0;
	}
	if (net_trace) {
		fprintf(stderr, "< %.200s\n", out->p);
	}
	return 0;
}

int net_bytes(Net *c, size_t n, Buf *out) {
	while (n > 0) {
		if (c->rpos >= c->rlen && fill(c) != 0) {
			return -1;
		}
		size_t take = c->rlen - c->rpos < n ? c->rlen - c->rpos : n;
		buf_put(out, c->rbuf + c->rpos, take);
		c->rpos += take;
		n -= take;
	}
	return 0;
}

void net_close(Net *c) {
	if (!c) {
		return;
	}
	if (c->ssl) {
		SSL_shutdown(c->ssl);
		SSL_free(c->ssl);
	}
	if (c->ctx) {
		SSL_CTX_free(c->ctx);
	}
	if (c->fd >= 0) {
		close(c->fd);
	}
	free(c);
}
