/* ------------------------------------------------------------------------
 *   A WebSocket client (RFC 6455) over TLS (wss://) or plain TCP (ws://,
 *   used by the offline stand-in server in tests/). The connect and
 *   handshake block, so they run on a short-lived thread; after that the
 *   socket is non-blocking and driven from the main poll loop.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_WS_H
#define SYN_CAST_WS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WS_TEXT = 1, WS_BINARY = 2, WS_CLOSE = 8, WS_PING = 9, WS_PONG = 10 };

typedef struct ws ws;

/* Blocking connect + TLS + upgrade. NULL with a reason in err. With a
 * cert (a PEM file) that certificate alone is trusted, pinned: the server
 * must present exactly it (or a chain it signs), and its host name is not
 * checked against it. "" or NULL trusts the system store and checks the
 * name. */
ws *ws_connect(const char *url, const char *cert, char *err, size_t errn);

/* What ws_connect_async() writes (as a pointer) to notify_fd. The reader
 * owns it and frees it with free(). */
typedef struct {
	uint64_t cookie;
	ws *conn;        /* NULL on failure */
	char err[200];
} ws_connect_result;
int ws_connect_async(const char *url, const char *cert, int notify_fd, uint64_t cookie);

int ws_fd(const ws *w);
/* The code of the peer's close frame (4003: replaced by a newer session of
 * this member and device; 4001, 4004: the token is refused), 0 when there
 * was none. */
int ws_close_code(const ws *w);
/* now_ms() of the last bytes from the peer, pings included: what proves
 * the link alive on a quiet channel. */
int64_t ws_last_rx(const ws *w);
bool ws_wants_write(const ws *w);
size_t ws_backlog(const ws *w);

/* Queues one frame whose payload is a||b (b may be NULL) and tries to
 * flush. Returns -1 once the connection is unusable. */
int ws_send(ws *w, int opcode, const void *a, size_t an, const void *b, size_t bn);
int ws_flush(ws *w);

/* Call when the fd is readable. Each complete text/binary message goes to
 * cb. The callback must not close w. Returns -1 when the peer closed or
 * the connection failed. */
typedef void (*ws_frame_cb)(void *ud, int opcode, const uint8_t *data, size_t n);
int ws_read(ws *w, ws_frame_cb cb, void *ud);

void ws_close(ws *w);

#endif
