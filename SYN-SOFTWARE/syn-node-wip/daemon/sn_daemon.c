/* ------------------------------------------------------------------------
 *                           S Y N - N O D E D
 *
 *   The daemon, one per machine. A single poll() loop owns everything:
 *
 *   - the TCP listener (links from members, pairing while a code shows)
 *   - UDP 47900: LAN beacons, pairing queries
 *   - one link per active member, Noise_IK, kept alive with pings and
 *     redialled with backoff when it drops
 *   - roster gossip: on connect both sides compare digests and swap
 *     entries if they differ; anything new is passed on to every link
 *   - the local control socket the CLI, station, menu and bar talk to
 *
 *   Nothing blocks: every socket is non-blocking with its own read and
 *   write buffers, so one slow or hostile connection can't stall the
 *   rest.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-NODE (daemon)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _DEFAULT_SOURCE

#include "sn_daemon.h"
#include "sn_identity.h"
#include "sn_mesh.h"
#include "sn_noise.h"
#include "sn_pair.h"
#include "sn_roster.h"
#include "sn_util.h"
#include "sn_wire.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define MAX_CONNS 256
#define MAX_CTL 32
#define MAX_PEERS SN_ROSTER_MAX

#define PING_EVERY_MS 15000
#define LINK_TIMEOUT_MS 45000
#define HANDSHAKE_TIMEOUT_MS 10000
#define BEACON_EVERY_MS 10000
#define BACKOFF_MIN_MS 2000
#define BACKOFF_MAX_MS 60000

/* ---- state ---- */

static sn_identity g_id;
static sn_roster g_roster;
static uint8_t g_secret[32];
static bool g_has_secret;
static bool g_roster_damaged; /* never found a new mesh over a damaged file */
static sn_addr g_addrs[SN_ADDR_MAX];
static int g_naddrs;
static uint16_t g_port;
static uint8_t g_prologue[sizeof(SN_LINK_PROLOGUE) - 1 + 32];

static int g_tcp = -1, g_udp = -1, g_ctl = -1;
static volatile sig_atomic_t g_stop;

typedef enum {
	C_FREE = 0,
	C_CONNECTING,     /* outbound, connect() in progress */
	C_IN_MAGIC,       /* inbound, waiting for the 4-byte magic */
	C_LINK_WAIT_MSG1, /* inbound link, waiting for Noise message 1 */
	C_LINK_WAIT_MSG2, /* outbound link, waiting for Noise message 2 */
	C_LINK_UP,
	C_PAIR_WAIT_Y,    /* pairing: sid sent, waiting for the joiner's point */
	C_PAIR_WAIT_ID,   /* pairing: point sent, waiting for the sealed id */
	C_CLOSING,        /* flush tx, then close */
} conn_state;

typedef struct {
	int fd;
	conn_state state;
	bool outbound;
	sn_buf rx, tx;
	struct sockaddr_in remote;
	uint64_t created_ms, last_rx_ms, last_ping_ms;
	/* link */
	sn_handshake hs;
	sn_cipher send, recv;
	uint8_t peer_pk[32];  /* outbound: who we meant to reach; inbound: who it turned out to be */
	uint16_t peer_port;
	char peer_name[32];
	int rtt_ms;
	/* pairing */
	sn_cpace cp;
	uint8_t k_j2m[32], k_m2j[32];
} conn;

typedef struct {
	uint8_t pk[32];
	int conn;               /* index of the live link, -1 if none */
	uint64_t next_dial_ms;
	uint64_t backoff_ms;
	int rtt_ms;
} peer;

typedef struct {
	int fd;
	sn_buf rx;
	bool pairing; /* this client holds the pairing window */
} ctl_client;

static conn g_conns[MAX_CONNS];
static peer g_peers[MAX_PEERS];
static int g_npeers;
static ctl_client g_ctls[MAX_CTL];

/* the pairing window: open while a ctl client is waiting on PAIR */
static struct {
	bool open;
	char code[SN_PAIR_CODE_DIGITS + 1];
	uint64_t until_ms;
	int ctl;      /* index into g_ctls */
	int conn;     /* the one pairing connection allowed at a time, -1 if none */
} g_pair = {.ctl = -1, .conn = -1};

/* ---- helpers ---- */

static void handle_signal(int sig) {
	(void)sig;
	g_stop = 1;
}

static void set_nonblock(int fd) {
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
	fcntl(fd, F_SETFD, FD_CLOEXEC);
}

static const char *pk_short(const uint8_t pk[32]) {
	static char buf[4][17];
	static int slot;
	slot = (slot + 1) % 4;
	sn_hex(pk, 8, buf[slot]);
	return buf[slot];
}

static const char *name_of(const uint8_t pk[32]) {
	const sn_member *m = sn_roster_find(&g_roster, pk);
	return m && m->name[0] ? m->name : pk_short(pk);
}

static void ctl_send(int ci, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void make_prologue(void) {
	size_t n = sizeof(SN_LINK_PROLOGUE) - 1;
	memcpy(g_prologue, SN_LINK_PROLOGUE, n);
	memcpy(g_prologue + n, g_roster.mesh_id, 32);
}

static int peer_index(const uint8_t pk[32]) {
	for (int i = 0; i < g_npeers; i++) {
		if (memcmp(g_peers[i].pk, pk, 32) == 0) return i;
	}
	return -1;
}

static const sn_addr *addr_of(const uint8_t pk[32]) {
	for (int i = 0; i < g_naddrs; i++) {
		if (memcmp(g_addrs[i].pk, pk, 32) == 0) return &g_addrs[i];
	}
	return NULL;
}

static void note_addr(const uint8_t pk[32], const char *ip, uint16_t port) {
	const sn_addr *old = addr_of(pk);
	bool changed = !old || strcmp(old->ip, ip) != 0 || old->port != port;
	g_naddrs = sn_addrs_set(g_addrs, g_naddrs, SN_ADDR_MAX, pk, ip, port, sn_unix_time());
	if (changed) sn_addrs_save(g_addrs, g_naddrs);
}

/* ---- connections ---- */

static void conn_close(int ci, const char *why) {
	conn *c = &g_conns[ci];
	if (c->state == C_FREE) return;
	if (c->state == C_LINK_UP) {
		sn_log("link to %s closed: %s", name_of(c->peer_pk), why);
	} else if (why && why[0]) {
		sn_log("connection from %s closed: %s", inet_ntoa(c->remote.sin_addr), why);
	}
	int pi = peer_index(c->peer_pk);
	if (pi >= 0 && g_peers[pi].conn == ci) {
		g_peers[pi].conn = -1;
		g_peers[pi].next_dial_ms = sn_now_ms() + g_peers[pi].backoff_ms;
	}
	if (g_pair.conn == ci) g_pair.conn = -1;
	close(c->fd);
	sn_buf_free(&c->rx);
	sn_buf_free(&c->tx);
	sodium_memzero(c, sizeof(*c));
	c->state = C_FREE;
	c->fd = -1;
}

static int conn_alloc(int fd, bool outbound, const struct sockaddr_in *remote) {
	for (int i = 0; i < MAX_CONNS; i++) {
		if (g_conns[i].state == C_FREE) {
			conn *c = &g_conns[i];
			memset(c, 0, sizeof(*c));
			c->fd = fd;
			c->outbound = outbound;
			c->remote = *remote;
			c->created_ms = c->last_rx_ms = sn_now_ms();
			return i;
		}
	}
	return -1;
}

static void conn_queue(int ci, const void *data, size_t len) {
	sn_buf_append(&g_conns[ci].tx, data, len);
}

/* Encrypts and queues one link message. */
static void link_send(int ci, uint8_t type, const uint8_t *payload, size_t len) {
	conn *c = &g_conns[ci];
	if (c->state != C_LINK_UP || len + 1 + SN_NOISE_TAGLEN > SN_MAX_LINK_FRAME) return;
	uint8_t plain[SN_MAX_LINK_FRAME], cipher[SN_MAX_LINK_FRAME];
	plain[0] = type;
	memcpy(plain + 1, payload, len);
	long n = sn_cipher_encrypt(&c->send, NULL, 0, plain, len + 1, cipher);
	if (n < 0) {
		conn_close(ci, "send cipher exhausted");
		return;
	}
	sn_frame_append(&c->tx, cipher, (size_t)n);
}

static void link_send_hello(int ci) {
	uint8_t p[34];
	sn_put16(p, SN_VERSION);
	sn_roster_digest(&g_roster, p + 2);
	link_send(ci, SN_MSG_HELLO, p, sizeof(p));
}

/* Sends entries [from, to) of the roster. */
static void link_send_entries(int ci, int from, int to) {
	static uint8_t p[2 + SN_ENTRIES_PER_MSG * SN_ENTRY_SIZE];
	while (from < to) {
		int n = to - from > SN_ENTRIES_PER_MSG ? SN_ENTRIES_PER_MSG : to - from;
		sn_put16(p, (uint16_t)n);
		for (int i = 0; i < n; i++) sn_entry_pack(&g_roster.entries[from + i], p + 2 + i * SN_ENTRY_SIZE);
		link_send(ci, SN_MSG_ENTRIES, p, 2 + (size_t)n * SN_ENTRY_SIZE);
		from += n;
	}
}

static void hello_payload(uint8_t out[SN_HELLO_PAYLOAD]) {
	memcpy(out, g_id.sign_pk, 32);
	sn_put16(out + 32, g_port);
	memcpy(out + 34, g_id.name, 32);
}

/* ---- peers: one per active member except us ---- */

static void peers_rebuild(void) {
	peer old[MAX_PEERS];
	int nold = g_npeers;
	memcpy(old, g_peers, sizeof(peer) * (size_t)nold);
	g_npeers = 0;
	for (int i = 0; i < g_roster.member_count; i++) {
		const sn_member *m = &g_roster.members[i];
		if (m->revoked || memcmp(m->pk, g_id.sign_pk, 32) == 0) continue;
		peer *p = &g_peers[g_npeers++];
		memset(p, 0, sizeof(*p));
		memcpy(p->pk, m->pk, 32);
		p->conn = -1;
		p->backoff_ms = BACKOFF_MIN_MS;
		p->rtt_ms = -1;
		for (int j = 0; j < nold; j++) {
			if (memcmp(old[j].pk, m->pk, 32) == 0) *p = old[j];
		}
	}
	/* drop links to anyone no longer active */
	for (int ci = 0; ci < MAX_CONNS; ci++) {
		conn *c = &g_conns[ci];
		if ((c->state == C_LINK_UP || c->state == C_LINK_WAIT_MSG2 || c->state == C_CONNECTING) &&
				!sn_roster_is_active(&g_roster, c->peer_pk)) {
			conn_close(ci, "revoked");
		}
	}
}

static bool self_active(void) {
	return g_roster.has_mesh && sn_roster_is_active(&g_roster, g_id.sign_pk);
}

/* After the roster changed: save, recompute peers, tell every link. */
static void roster_changed(int first_new, int from_conn) {
	sn_roster_save(&g_roster);
	make_prologue();
	if (!self_active()) {
		sn_log("this node is no longer an active member of its mesh");
		for (int ci = 0; ci < MAX_CONNS; ci++) {
			if (g_conns[ci].state == C_LINK_UP) conn_close(ci, "we were revoked");
		}
	}
	/* pass the new entries on before dropping anyone they revoke, so
	 * a revoked node hears it */
	for (int ci = 0; ci < MAX_CONNS; ci++) {
		if (ci != from_conn && g_conns[ci].state == C_LINK_UP) {
			link_send_entries(ci, first_new, g_roster.count);
		}
	}
	peers_rebuild();
}

/* ---- dialling ---- */

static void dial(int pi) {
	peer *p = &g_peers[pi];
	const sn_addr *a = addr_of(p->pk);
	uint64_t now = sn_now_ms();
	p->next_dial_ms = now + p->backoff_ms;
	p->backoff_ms = p->backoff_ms * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : p->backoff_ms * 2;
	if (!a) return;

	struct sockaddr_in sa = {0};
	sa.sin_family = AF_INET;
	sa.sin_port = htons(a->port);
	if (inet_pton(AF_INET, a->ip, &sa.sin_addr) != 1) return;

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return;
	set_nonblock(fd);
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	int ci = conn_alloc(fd, true, &sa);
	if (ci < 0) {
		close(fd);
		return;
	}
	memcpy(g_conns[ci].peer_pk, p->pk, 32);
	int rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
	if (rc != 0 && errno != EINPROGRESS) {
		conn_close(ci, "");
		return;
	}
	g_conns[ci].state = C_CONNECTING;
}

/* connect() finished: send the magic and Noise message 1 */
static void link_start_outbound(int ci) {
	conn *c = &g_conns[ci];
	int err = 0;
	socklen_t len = sizeof(err);
	if (getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
		conn_close(ci, "");
		return;
	}
	uint8_t rs[32];
	if (!sn_identity_dh_of(c->peer_pk, rs)) {
		conn_close(ci, "bad peer key");
		return;
	}
	sn_noise_init(&c->hs, true, g_prologue, sizeof(g_prologue), g_id.dh_priv, g_id.dh_pub, rs);
	uint8_t payload[SN_HELLO_PAYLOAD], msg[SN_HELLO_PAYLOAD + 128];
	hello_payload(payload);
	long n = sn_noise_write_msg1(&c->hs, payload, sizeof(payload), msg);
	if (n < 0) {
		conn_close(ci, "handshake");
		return;
	}
	conn_queue(ci, SN_MAGIC_LINK, 4);
	sn_frame_append(&c->tx, msg, (size_t)n);
	c->state = C_LINK_WAIT_MSG2;
}

/* A link just came up. Keep one link per peer: if there are two (both
 * sides dialled at once), keep the one dialled by the lower key, which
 * both sides agree on without talking. */
static void link_established(int ci) {
	conn *c = &g_conns[ci];
	c->state = C_LINK_UP;
	c->last_rx_ms = c->last_ping_ms = sn_now_ms();
	c->rtt_ms = -1;
	int pi = peer_index(c->peer_pk);
	if (pi < 0) {
		conn_close(ci, "not a peer");
		return;
	}
	peer *p = &g_peers[pi];
	if (p->conn >= 0 && p->conn != ci && g_conns[p->conn].state == C_LINK_UP) {
		int other = p->conn;
		bool lower_is_me = memcmp(g_id.sign_pk, c->peer_pk, 32) < 0;
		/* the link dialled by the lower key: outbound if I'm lower */
		bool keep_new = c->outbound == lower_is_me;
		if (!keep_new) {
			/* flush what's queued (message 2) and hang up, so the other
			 * side sees a finished handshake and a clean close, not a
			 * refusal */
			c->state = C_CLOSING;
			return;
		}
		p->conn = -1;
		g_conns[other].state = C_CLOSING;
		sn_log("link to %s: keeping the newer of two links", name_of(c->peer_pk));
	}
	p->conn = ci;
	p->backoff_ms = BACKOFF_MIN_MS;
	note_addr(c->peer_pk, inet_ntoa(c->remote.sin_addr),
		c->outbound ? ntohs(c->remote.sin_port) : c->peer_port);
	sn_log("link to %s up (%s, %s)", name_of(c->peer_pk), inet_ntoa(c->remote.sin_addr),
		c->outbound ? "dialled" : "answered");
	link_send_hello(ci);
}

/* ---- handling frames ---- */

static void handle_link_msg(int ci, const uint8_t *m, size_t len) {
	conn *c = &g_conns[ci];
	if (len < 1) return;
	uint8_t type = m[0];
	const uint8_t *p = m + 1;
	size_t plen = len - 1;

	switch (type) {
	case SN_MSG_HELLO: {
		if (plen < 34) return;
		uint8_t mine[32];
		sn_roster_digest(&g_roster, mine);
		if (memcmp(mine, p + 2, 32) != 0) {
			link_send_entries(ci, 0, g_roster.count);
		}
		break;
	}
	case SN_MSG_ENTRIES: {
		if (plen < 2) return;
		int n = sn_get16(p);
		if (plen < 2 + (size_t)n * SN_ENTRY_SIZE) return;
		int first_new = g_roster.count;
		bool any = false;
		for (int i = 0; i < n; i++) {
			sn_entry e;
			sn_entry_unpack(p + 2 + i * SN_ENTRY_SIZE, &e);
			if (sn_roster_add(&g_roster, &e)) any = true;
		}
		if (any) {
			sn_log("roster: %d new entr%s from %s", g_roster.count - first_new,
				g_roster.count - first_new == 1 ? "y" : "ies", name_of(c->peer_pk));
			roster_changed(first_new, ci);
		}
		break;
	}
	case SN_MSG_PING:
		if (plen >= 8) link_send(ci, SN_MSG_PONG, p, 8);
		break;
	case SN_MSG_PONG:
		if (plen >= 8) {
			uint64_t sent = sn_get64(p);
			uint64_t now = sn_now_ms();
			c->rtt_ms = now >= sent ? (int)(now - sent) : -1;
			int pi = peer_index(c->peer_pk);
			if (pi >= 0) g_peers[pi].rtt_ms = c->rtt_ms;
		}
		break;
	default:
		break; /* newer message types: ignore */
	}
}

static void pair_finish(bool ok, const char *msg) {
	if (g_pair.ctl >= 0) {
		ctl_send(g_pair.ctl, "%s %s\n", ok ? "OK" : "FAIL", msg);
		g_ctls[g_pair.ctl].pairing = false;
	}
	sodium_memzero(g_pair.code, sizeof(g_pair.code));
	g_pair.open = false;
	g_pair.ctl = -1;
}

/* The joiner proved the code. Admit them (founding the mesh first if
 * this node has none) and send the mesh back. */
static void pair_admit(int ci, const uint8_t *id, size_t len) {
	conn *c = &g_conns[ci];
	if (len != SN_PAIR_ID_LEN) {
		conn_close(ci, "pairing: bad id");
		pair_finish(false, "the other machine sent something unexpected");
		return;
	}
	const uint8_t *jpk = id;
	uint16_t jport = sn_get16(id + 32);
	char jname[32];
	memcpy(jname, id + 34, 32);
	jname[31] = '\0';
	if (memcmp(jpk, g_id.sign_pk, 32) == 0) {
		conn_close(ci, "pairing with itself");
		pair_finish(false, "that is this machine");
		return;
	}
	const sn_member *existing = sn_roster_find(&g_roster, jpk);
	if (existing && existing->revoked) {
		conn_close(ci, "pairing: revoked key");
		pair_finish(false, "that machine was revoked; it needs a new identity to rejoin");
		return;
	}

	int first_new;
	if (!g_roster.has_mesh) {
		sn_roster_found(&g_roster, g_id.sign_pk, g_id.sign_sk, g_id.name);
		randombytes_buf(g_secret, 32);
		g_has_secret = sn_secret_save(g_secret);
		make_prologue();
		sn_log("founded a new mesh");
		first_new = 0;
	} else {
		first_new = g_roster.count;
	}
	if (!existing) {
		sn_entry e = {0};
		e.kind = SN_ENTRY_MEMBER;
		memcpy(e.pk, jpk, 32);
		sn_name_field(jname, e.name);
		memcpy(e.signer, g_id.sign_pk, 32);
		e.time = sn_unix_time();
		sn_entry_sign(g_roster.mesh_id, &e, g_id.sign_sk);
		sn_roster_add(&g_roster, &e);
	}
	note_addr(jpk, inet_ntoa(c->remote.sin_addr), jport);

	size_t rlen;
	uint8_t *rblob = sn_roster_pack(&g_roster, &rlen);
	size_t plen = 32 + 2 + 32 + 32 + rlen;
	uint8_t *plain = malloc(plen), *sealed = malloc(plen + SN_PAIR_SEAL_EXTRA);
	if (!rblob || !plain || !sealed) {
		free(rblob); free(plain); free(sealed);
		conn_close(ci, "out of memory");
		pair_finish(false, "out of memory");
		return;
	}
	memcpy(plain, g_secret, 32);
	sn_put16(plain + 32, g_port);
	memcpy(plain + 34, g_id.name, 32);
	memcpy(plain + 66, g_id.sign_pk, 32);
	memcpy(plain + 98, rblob, rlen);
	sn_pair_seal(c->k_m2j, plain, plen, sealed);
	sn_frame_append(&c->tx, sealed, plen + SN_PAIR_SEAL_EXTRA);
	sodium_memzero(plain, plen);
	free(plain); free(sealed); free(rblob);
	c->state = C_CLOSING;

	sn_log("paired: %s joined the mesh", jname);
	roster_changed(first_new, -1);
	char msg[64];
	snprintf(msg, sizeof(msg), "%s", jname);
	pair_finish(true, msg);
}

/* Returns false if the connection was closed. */
static bool handle_frame(int ci, const uint8_t *f, size_t len) {
	conn *c = &g_conns[ci];
	switch (c->state) {
	case C_LINK_WAIT_MSG1: {
		if (!g_roster.has_mesh) {
			conn_close(ci, "no mesh yet");
			return false;
		}
		sn_noise_init(&c->hs, false, g_prologue, sizeof(g_prologue), g_id.dh_priv, g_id.dh_pub, NULL);
		uint8_t payload[SN_MAX_LINK_FRAME];
		long n = sn_noise_read_msg1(&c->hs, f, len, payload);
		if (n != SN_HELLO_PAYLOAD) {
			conn_close(ci, "refused: handshake failed (not our mesh, or not for us)");
			return false;
		}
		uint8_t dh[32];
		if (!sn_identity_dh_of(payload, dh) || memcmp(dh, c->hs.rs, 32) != 0) {
			conn_close(ci, "refused: claimed key doesn't match its handshake key");
			return false;
		}
		if (!sn_roster_is_active(&g_roster, payload) || memcmp(payload, g_id.sign_pk, 32) == 0) {
			char why[96];
			snprintf(why, sizeof(why), "refused: key %s is %s", pk_short(payload),
				sn_roster_find(&g_roster, payload) ? "revoked" : "not in the roster");
			conn_close(ci, why);
			return false;
		}
		memcpy(c->peer_pk, payload, 32);
		c->peer_port = sn_get16(payload + 32);
		memcpy(c->peer_name, payload + 34, 32);
		c->peer_name[31] = '\0';
		uint8_t reply[SN_HELLO_PAYLOAD], msg[SN_HELLO_PAYLOAD + 64];
		hello_payload(reply);
		long m = sn_noise_write_msg2(&c->hs, reply, sizeof(reply), msg);
		if (m < 0) {
			conn_close(ci, "handshake");
			return false;
		}
		sn_frame_append(&c->tx, msg, (size_t)m);
		sn_noise_split(&c->hs, &c->send, &c->recv, NULL);
		link_established(ci);
		return g_conns[ci].state == C_LINK_UP;
	}
	case C_LINK_WAIT_MSG2: {
		uint8_t payload[SN_MAX_LINK_FRAME];
		long n = sn_noise_read_msg2(&c->hs, f, len, payload);
		if (n != SN_HELLO_PAYLOAD || memcmp(payload, c->peer_pk, 32) != 0) {
			conn_close(ci, "handshake failed");
			return false;
		}
		c->peer_port = sn_get16(payload + 32);
		sn_noise_split(&c->hs, &c->send, &c->recv, NULL);
		link_established(ci);
		return g_conns[ci].state == C_LINK_UP;
	}
	case C_LINK_UP: {
		uint8_t plain[SN_MAX_LINK_FRAME];
		long n = sn_cipher_decrypt(&c->recv, NULL, 0, f, len, plain);
		if (n < 0) {
			conn_close(ci, "bad message");
			return false;
		}
		c->last_rx_ms = sn_now_ms();
		handle_link_msg(ci, plain, (size_t)n);
		return g_conns[ci].state == C_LINK_UP;
	}
	case C_PAIR_WAIT_Y: {
		if (len != 32 || !g_pair.open) {
			conn_close(ci, "pairing: bad point");
			return false;
		}
		uint8_t Ym[32];
		memcpy(Ym, c->cp.Y, 32);
		if (!sn_cpace_finish(&c->cp, f, c->k_j2m, c->k_m2j)) {
			conn_close(ci, "pairing: bad point");
			return false;
		}
		sn_frame_append(&c->tx, Ym, 32);
		c->state = C_PAIR_WAIT_ID;
		return true;
	}
	case C_PAIR_WAIT_ID: {
		uint8_t id[SN_PAIR_ID_LEN];
		if (len != SN_PAIR_ID_LEN + SN_PAIR_SEAL_EXTRA || !sn_pair_open(c->k_j2m, f, len, id)) {
			/* wrong code: burn it, one guess per code */
			conn_close(ci, "pairing: wrong code");
			pair_finish(false, "wrong code; the code is spent, run pair again");
			return false;
		}
		pair_admit(ci, id, len - SN_PAIR_SEAL_EXTRA);
		return false; /* now closing */
	}
	default:
		conn_close(ci, "unexpected data");
		return false;
	}
}

static void conn_on_magic(int ci) {
	conn *c = &g_conns[ci];
	if (memcmp(c->rx.data, SN_MAGIC_LINK, 4) == 0) {
		sn_buf_consume(&c->rx, 4);
		c->state = C_LINK_WAIT_MSG1;
		return;
	}
	if (memcmp(c->rx.data, SN_MAGIC_PAIR, 4) == 0) {
		sn_buf_consume(&c->rx, 4);
		if (!g_pair.open || g_pair.conn >= 0) {
			conn_close(ci, g_pair.open ? "pairing busy" : "no pairing code showing");
			return;
		}
		uint8_t sid[SN_PAIR_SID_LEN];
		randombytes_buf(sid, sizeof(sid));
		if (!sn_cpace_start(&c->cp, false, g_pair.code, sid)) {
			conn_close(ci, "pairing");
			return;
		}
		g_pair.conn = ci;
		sn_frame_append(&c->tx, sid, sizeof(sid));
		c->state = C_PAIR_WAIT_Y;
		return;
	}
	conn_close(ci, "not syn-node");
}

static void conn_readable(int ci) {
	conn *c = &g_conns[ci];
	uint8_t chunk[16384];
	for (;;) {
		ssize_t n = read(c->fd, chunk, sizeof(chunk));
		if (n > 0) {
			if (c->rx.len + (size_t)n > SN_MAX_FRAME + 8) {
				conn_close(ci, "too much data");
				return;
			}
			sn_buf_append(&c->rx, chunk, (size_t)n);
			continue;
		}
		if (n == 0) {
			conn_close(ci, c->state == C_LINK_UP ? "peer hung up" : "");
			return;
		}
		if (errno == EINTR) continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK) break;
		conn_close(ci, strerror(errno));
		return;
	}

	for (;;) {
		c = &g_conns[ci];
		if (c->state == C_FREE || c->state == C_CLOSING) return;
		if (c->state == C_IN_MAGIC) {
			if (c->rx.len < 4) return;
			conn_on_magic(ci);
			continue;
		}
		const uint8_t *f;
		size_t flen;
		size_t max = c->state == C_LINK_UP || c->state == C_LINK_WAIT_MSG1 || c->state == C_LINK_WAIT_MSG2
			? SN_MAX_LINK_FRAME : 4096;
		long used = sn_frame_peek(&c->rx, max, &f, &flen);
		if (used < 0) {
			conn_close(ci, "frame too large");
			return;
		}
		if (used == 0) return;
		/* copy out: handle_frame may append to tx but never touches rx,
		 * still, keep the frame stable while it runs */
		uint8_t *copy = malloc(flen ? flen : 1);
		if (!copy) {
			conn_close(ci, "out of memory");
			return;
		}
		memcpy(copy, f, flen);
		sn_buf_consume(&c->rx, (size_t)used);
		bool alive = handle_frame(ci, copy, flen);
		free(copy);
		if (!alive) return;
	}
}

static void conn_writable(int ci) {
	conn *c = &g_conns[ci];
	if (c->state == C_CONNECTING) {
		link_start_outbound(ci);
		return;
	}
	while (c->tx.len > 0) {
		ssize_t n = write(c->fd, c->tx.data, c->tx.len);
		if (n > 0) {
			sn_buf_consume(&c->tx, (size_t)n);
			continue;
		}
		if (n < 0 && errno == EINTR) continue;
		if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
		conn_close(ci, "write failed");
		return;
	}
	if (c->state == C_CLOSING) conn_close(ci, "");
}

static void accept_tcp(void) {
	for (;;) {
		struct sockaddr_in sa;
		socklen_t len = sizeof(sa);
		int fd = accept(g_tcp, (struct sockaddr *)&sa, &len);
		if (fd < 0) return;
		set_nonblock(fd);
		int one = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		int ci = conn_alloc(fd, false, &sa);
		if (ci < 0) {
			close(fd);
			continue;
		}
		g_conns[ci].state = C_IN_MAGIC;
	}
}

/* ---- UDP: beacons and pairing queries ---- */

static int broadcast_addrs(struct sockaddr_in *out, int max) {
	struct ifaddrs *ifs, *it;
	int n = 0;
	if (getifaddrs(&ifs) == 0) {
		for (it = ifs; it && n < max; it = it->ifa_next) {
			if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
			if (!(it->ifa_flags & IFF_UP) || !(it->ifa_flags & IFF_BROADCAST) || !it->ifa_broadaddr) continue;
			out[n] = *(struct sockaddr_in *)it->ifa_broadaddr;
			out[n].sin_port = htons(SN_UDP_PORT);
			n++;
		}
		freeifaddrs(ifs);
	}
	/* always loop back too, so two daemons on one machine (tests) hear
	 * each other */
	if (n < max) {
		memset(&out[n], 0, sizeof(out[n]));
		out[n].sin_family = AF_INET;
		out[n].sin_port = htons(SN_UDP_PORT);
		out[n].sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		n++;
	}
	return n;
}

static void send_beacon(void) {
	if (!g_has_secret || !self_active()) return;
	uint8_t plain[42], pkt[4 + 24 + 42 + crypto_secretbox_MACBYTES];
	memcpy(plain, g_id.sign_pk, 32);
	sn_put16(plain + 32, g_port);
	sn_put64(plain + 34, sn_unix_time());
	memcpy(pkt, "SNB1", 4);
	randombytes_buf(pkt + 4, 24);
	crypto_secretbox_easy(pkt + 28, plain, sizeof(plain), pkt + 4, g_secret);
	struct sockaddr_in dst[16];
	int n = broadcast_addrs(dst, 16);
	for (int i = 0; i < n; i++) {
		sendto(g_udp, pkt, sizeof(pkt), 0, (struct sockaddr *)&dst[i], sizeof(dst[i]));
	}
}

static void udp_readable(void) {
	for (;;) {
		uint8_t pkt[512];
		struct sockaddr_in from;
		socklen_t flen = sizeof(from);
		ssize_t n = recvfrom(g_udp, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &flen);
		if (n < 0) return;
		if (n == 4 && memcmp(pkt, "SNPQ", 4) == 0) {
			if (g_pair.open && g_pair.conn < 0) {
				uint8_t reply[6];
				memcpy(reply, "SNPA", 4);
				sn_put16(reply + 4, g_port);
				sendto(g_udp, reply, sizeof(reply), 0, (struct sockaddr *)&from, flen);
			}
			continue;
		}
		if (n == 4 + 24 + 42 + crypto_secretbox_MACBYTES && memcmp(pkt, "SNB1", 4) == 0 && g_has_secret) {
			uint8_t plain[42];
			if (crypto_secretbox_open_easy(plain, pkt + 28, (unsigned long long)n - 28, pkt + 4, g_secret) != 0) continue;
			if (memcmp(plain, g_id.sign_pk, 32) == 0) continue; /* our own */
			int pi = peer_index(plain);
			if (pi < 0) continue;
			uint16_t port = sn_get16(plain + 32);
			const sn_addr *old = addr_of(plain);
			bool moved = !old || strcmp(old->ip, inet_ntoa(from.sin_addr)) != 0 || old->port != port;
			if (moved || g_peers[pi].conn < 0) {
				note_addr(plain, inet_ntoa(from.sin_addr), port);
				if (g_peers[pi].conn < 0) {
					g_peers[pi].backoff_ms = BACKOFF_MIN_MS;
					g_peers[pi].next_dial_ms = 0; /* dial now */
				}
			}
		}
	}
}

/* ---- control socket ---- */

static void ctl_send(int ci, const char *fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n <= 0 || g_ctls[ci].fd < 0) return;
	/* replies are small and the client is local: a short blocking write */
	sn_write_all(g_ctls[ci].fd, buf, (size_t)(n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1), 1000);
}

static void ctl_close(int ci) {
	if (g_ctls[ci].fd < 0) return;
	if (g_ctls[ci].pairing) {
		/* the client went away: close the window */
		g_ctls[ci].pairing = false;
		g_pair.ctl = -1;
		pair_finish(false, "cancelled");
		if (g_pair.conn >= 0) conn_close(g_pair.conn, "pairing cancelled");
	}
	close(g_ctls[ci].fd);
	sn_buf_free(&g_ctls[ci].rx);
	g_ctls[ci].fd = -1;
}

static void cmd_nodes(int ci) {
	char hex[65];
	sn_hex(g_id.sign_pk, 32, hex);
	ctl_send(ci, "%s|%s|self|-|-\n", g_id.name, hex);
	for (int i = 0; i < g_roster.member_count; i++) {
		const sn_member *m = &g_roster.members[i];
		if (memcmp(m->pk, g_id.sign_pk, 32) == 0) continue;
		sn_hex(m->pk, 32, hex);
		const sn_addr *a = addr_of(m->pk);
		char addr[64] = "-";
		if (a) snprintf(addr, sizeof(addr), "%s:%u", a->ip, a->port);
		if (m->revoked) {
			ctl_send(ci, "%s|%s|revoked|%s|-\n", m->name, hex, addr);
			continue;
		}
		int pi = peer_index(m->pk);
		bool up = pi >= 0 && g_peers[pi].conn >= 0 && g_conns[g_peers[pi].conn].state == C_LINK_UP;
		char rtt[16] = "-";
		if (up && g_peers[pi].rtt_ms >= 0) snprintf(rtt, sizeof(rtt), "%d", g_peers[pi].rtt_ms);
		ctl_send(ci, "%s|%s|%s|%s|%s\n", m->name, hex, up ? "online" : "offline", addr, rtt);
	}
	ctl_send(ci, ".\n");
}

static void reload_state(void) {
	g_roster_damaged = !sn_roster_load(&g_roster);
	g_has_secret = sn_secret_load(g_secret);
	g_naddrs = sn_addrs_load(g_addrs, SN_ADDR_MAX);
	make_prologue();
	peers_rebuild();
	for (int i = 0; i < g_npeers; i++) g_peers[i].next_dial_ms = 0;
}

static void ctl_command(int ci, char *line) {
	if (strcmp(line, "STATUS") == 0) {
		char hex[65], mesh[65] = "none";
		sn_hex(g_id.sign_pk, 32, hex);
		if (g_roster.has_mesh) sn_hex(g_roster.mesh_id, 32, mesh);
		int online = 0;
		for (int i = 0; i < g_npeers; i++) {
			if (g_peers[i].conn >= 0 && g_conns[g_peers[i].conn].state == C_LINK_UP) online++;
		}
		ctl_send(ci, "name %s\nkey %s\nmesh %s\nport %u\nmembers %d\nonline %d\nactive %s\n.\n",
			g_id.name, hex, mesh, g_port, g_roster.member_count, online,
			self_active() ? "yes" : (g_roster.has_mesh ? "revoked" : "no-mesh"));
	} else if (strcmp(line, "NODES") == 0) {
		cmd_nodes(ci);
	} else if (strcmp(line, "PAIR") == 0) {
		if (g_roster_damaged) {
			ctl_send(ci, "FAIL the roster file is damaged; 'syn-node leave --yes' clears it\n");
			return;
		}
		if (g_roster.has_mesh && !self_active()) {
			ctl_send(ci, "FAIL this node was revoked from its mesh\n");
			return;
		}
		if (g_pair.open) {
			ctl_send(ci, "FAIL a pairing code is already showing\n");
			return;
		}
		sn_pair_new_code(g_pair.code);
		g_pair.open = true;
		g_pair.until_ms = sn_now_ms() + SN_PAIR_WINDOW_MS;
		g_pair.ctl = ci;
		g_pair.conn = -1;
		g_ctls[ci].pairing = true;
		ctl_send(ci, "CODE %.4s %.4s\n", g_pair.code, g_pair.code + 4);
		sn_log("pairing code showing for 5 minutes");
	} else if (strcmp(line, "RELOAD") == 0) {
		reload_state();
		sn_log("state reloaded: %d member(s)", g_roster.member_count);
		ctl_send(ci, "OK\n");
	} else if (strncmp(line, "REVOKE ", 7) == 0) {
		if (!self_active()) {
			ctl_send(ci, "FAIL not an active member of a mesh\n");
			return;
		}
		const sn_member *m = sn_roster_lookup(&g_roster, line + 7);
		if (!m) {
			ctl_send(ci, "FAIL no single node matches '%s'\n", line + 7);
			return;
		}
		if (memcmp(m->pk, g_id.sign_pk, 32) == 0) {
			ctl_send(ci, "FAIL that is this node; use leave\n");
			return;
		}
		if (m->revoked) {
			ctl_send(ci, "FAIL %s is already revoked\n", m->name);
			return;
		}
		sn_entry e = {0};
		e.kind = SN_ENTRY_REVOKE;
		memcpy(e.pk, m->pk, 32);
		memcpy(e.name, m->name, 32);
		memcpy(e.signer, g_id.sign_pk, 32);
		e.time = sn_unix_time();
		char name[32];
		memcpy(name, m->name, 32);
		sn_entry_sign(g_roster.mesh_id, &e, g_id.sign_sk);
		int first_new = g_roster.count;
		sn_roster_add(&g_roster, &e);
		sn_log("revoked %s", name);
		roster_changed(first_new, -1);
		ctl_send(ci, "OK %s\n", name);
	} else if (strcmp(line, "LEAVE") == 0) {
		for (int c = 0; c < MAX_CONNS; c++) {
			if (g_conns[c].state != C_FREE) conn_close(c, "left the mesh");
		}
		sn_mesh_forget();
		reload_state();
		sn_log("left the mesh");
		ctl_send(ci, "OK\n");
	} else {
		ctl_send(ci, "FAIL unknown command\n");
	}
}

static void ctl_readable(int ci) {
	char chunk[512];
	ssize_t n = read(g_ctls[ci].fd, chunk, sizeof(chunk));
	if (n <= 0) {
		if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
		ctl_close(ci);
		return;
	}
	sn_buf_append(&g_ctls[ci].rx, chunk, (size_t)n);
	if (g_ctls[ci].rx.len > 4096) {
		ctl_close(ci);
		return;
	}
	for (;;) {
		uint8_t *nl = memchr(g_ctls[ci].rx.data, '\n', g_ctls[ci].rx.len);
		if (!nl) return;
		size_t len = (size_t)(nl - g_ctls[ci].rx.data);
		char line[4096];
		memcpy(line, g_ctls[ci].rx.data, len);
		line[len] = '\0';
		if (len && line[len - 1] == '\r') line[len - 1] = '\0';
		sn_buf_consume(&g_ctls[ci].rx, len + 1);
		ctl_command(ci, line);
		if (g_ctls[ci].fd < 0) return;
	}
}

static void accept_ctl(void) {
	for (;;) {
		int fd = accept(g_ctl, NULL, NULL);
		if (fd < 0) return;
		set_nonblock(fd);
		int slot = -1;
		for (int i = 0; i < MAX_CTL; i++) {
			if (g_ctls[i].fd < 0) { slot = i; break; }
		}
		if (slot < 0) {
			close(fd);
			continue;
		}
		memset(&g_ctls[slot], 0, sizeof(g_ctls[slot]));
		g_ctls[slot].fd = fd;
	}
}

/* ---- sockets ---- */

static int open_tcp(uint16_t port) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in sa = {0};
	sa.sin_family = AF_INET;
	sa.sin_port = htons(port);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(fd, 32) != 0) {
		close(fd);
		return -1;
	}
	set_nonblock(fd);
	return fd;
}

/* UDP 47900, shared: SO_REUSEADDR lets every syn-noded on the machine
 * bind it, and the kernel hands each of them every broadcast. */
static int open_udp(void) {
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) return -1;
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
	struct sockaddr_in sa = {0};
	sa.sin_family = AF_INET;
	sa.sin_port = htons(SN_UDP_PORT);
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
		close(fd);
		return -1;
	}
	set_nonblock(fd);
	return fd;
}

static int open_ctl(const char *path) {
	/* refuse to start twice: if something answers on the socket, it's
	 * another daemon */
	struct sockaddr_un sa = {0};
	if (strlen(path) >= sizeof(sa.sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}
	sa.sun_family = AF_UNIX;
	memcpy(sa.sun_path, path, strlen(path) + 1);
	int probe = socket(AF_UNIX, SOCK_STREAM, 0);
	if (probe >= 0 && connect(probe, (struct sockaddr *)&sa, sizeof(sa)) == 0) {
		close(probe);
		return -2;
	}
	if (probe >= 0) close(probe);
	unlink(path);
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	mode_t old = umask(0177);
	int rc = bind(fd, (struct sockaddr *)&sa, sizeof(sa));
	umask(old);
	if (rc != 0 || listen(fd, 8) != 0) {
		close(fd);
		return -1;
	}
	set_nonblock(fd);
	return fd;
}

/* ---- main loop ---- */

int sn_daemon_run(void) {
	if (sodium_init() < 0) {
		sn_log("libsodium failed to start");
		return 1;
	}
	if (!sn_identity_load(&g_id)) return 1;
	g_port = sn_port();
	for (int i = 0; i < MAX_CONNS; i++) g_conns[i].fd = -1;
	for (int i = 0; i < MAX_CTL; i++) g_ctls[i].fd = -1;

	char ctl_path[512];
	sn_ctl_path(ctl_path, sizeof(ctl_path));
	g_ctl = open_ctl(ctl_path);
	if (g_ctl == -2) {
		sn_log("already running (%s answers)", ctl_path);
		return 1;
	}
	g_tcp = open_tcp(g_port);
	g_udp = open_udp();
	if (g_ctl < 0 || g_tcp < 0) {
		sn_log("cannot open %s: %s", g_ctl < 0 ? ctl_path : "the TCP port", strerror(errno));
		return 1;
	}
	if (g_udp < 0) sn_log("UDP %d unavailable: no beacons or code discovery", SN_UDP_PORT);

	struct sigaction sa = {0};
	sa.sa_handler = handle_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	signal(SIGPIPE, SIG_IGN);

	reload_state();
	char hex[65];
	sn_hex(g_id.sign_pk, 32, hex);
	sn_log("%s up: key %.16s, port %u, %s", g_id.name, hex, g_port,
		g_roster.has_mesh ? "in a mesh" : "no mesh yet (pair or join)");

	uint64_t next_beacon = 0;
	while (!g_stop) {
		uint64_t now = sn_now_ms();

		/* timers */
		if (now >= next_beacon && g_udp >= 0) {
			send_beacon();
			next_beacon = now + BEACON_EVERY_MS;
		}
		if (g_pair.open && now >= g_pair.until_ms) {
			sn_log("pairing code expired");
			pair_finish(false, "the code expired");
		}
		for (int i = 0; i < g_npeers; i++) {
			if (g_peers[i].conn < 0 && now >= g_peers[i].next_dial_ms && self_active()) dial(i);
		}
		for (int ci = 0; ci < MAX_CONNS; ci++) {
			conn *c = &g_conns[ci];
			if (c->state == C_FREE) continue;
			if (c->state == C_LINK_UP) {
				if (now - c->last_rx_ms > LINK_TIMEOUT_MS) {
					conn_close(ci, "timed out");
				} else if (now - c->last_ping_ms > PING_EVERY_MS) {
					uint8_t stamp[8];
					sn_put64(stamp, now);
					link_send(ci, SN_MSG_PING, stamp, 8);
					c->last_ping_ms = now;
				}
			} else if (now - c->created_ms > HANDSHAKE_TIMEOUT_MS) {
				conn_close(ci, c->outbound ? "" : "handshake timed out");
			}
		}

		/* poll */
		struct pollfd pfds[3 + MAX_CONNS + MAX_CTL];
		int kinds[3 + MAX_CONNS + MAX_CTL]; /* -1 tcp, -2 udp, -3 ctl, 0..: conn, 1000..: ctl client */
		int np = 0;
		pfds[np] = (struct pollfd){.fd = g_tcp, .events = POLLIN}; kinds[np++] = -1;
		if (g_udp >= 0) { pfds[np] = (struct pollfd){.fd = g_udp, .events = POLLIN}; kinds[np++] = -2; }
		pfds[np] = (struct pollfd){.fd = g_ctl, .events = POLLIN}; kinds[np++] = -3;
		for (int ci = 0; ci < MAX_CONNS; ci++) {
			conn *c = &g_conns[ci];
			if (c->state == C_FREE) continue;
			short ev = 0;
			if (c->state == C_CONNECTING) ev = POLLOUT;
			else {
				if (c->state != C_CLOSING) ev |= POLLIN;
				if (c->tx.len) ev |= POLLOUT;
			}
			if (c->state == C_CLOSING && !c->tx.len) { conn_close(ci, ""); continue; }
			pfds[np] = (struct pollfd){.fd = c->fd, .events = ev}; kinds[np++] = ci;
		}
		for (int i = 0; i < MAX_CTL; i++) {
			if (g_ctls[i].fd < 0) continue;
			pfds[np] = (struct pollfd){.fd = g_ctls[i].fd, .events = POLLIN}; kinds[np++] = 1000 + i;
		}

		int rc = poll(pfds, (nfds_t)np, 500);
		if (rc < 0) {
			if (errno == EINTR) continue;
			sn_log("poll: %s", strerror(errno));
			break;
		}
		for (int i = 0; i < np; i++) {
			short re = pfds[i].revents;
			if (!re) continue;
			int k = kinds[i];
			if (k == -1) accept_tcp();
			else if (k == -2) udp_readable();
			else if (k == -3) accept_ctl();
			else if (k >= 1000) {
				int c = k - 1000;
				if (g_ctls[c].fd == pfds[i].fd) ctl_readable(c);
			} else {
				if (g_conns[k].fd != pfds[i].fd || g_conns[k].state == C_FREE) continue;
				if (re & (POLLERR | POLLHUP) && g_conns[k].state == C_CONNECTING) {
					conn_close(k, "");
					continue;
				}
				if (re & POLLOUT) conn_writable(k);
				if (g_conns[k].state != C_FREE && g_conns[k].fd == pfds[i].fd && (re & (POLLIN | POLLHUP | POLLERR))) {
					conn_readable(k);
				}
			}
		}
	}

	sn_log("stopping");
	for (int ci = 0; ci < MAX_CONNS; ci++) {
		if (g_conns[ci].state != C_FREE) conn_close(ci, "shutting down");
	}
	for (int i = 0; i < MAX_CTL; i++) ctl_close(i);
	close(g_tcp);
	if (g_udp >= 0) close(g_udp);
	close(g_ctl);
	unlink(ctl_path);
	sodium_memzero(&g_id, sizeof(g_id));
	return 0;
}
