/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "app.h"
#include "image.h"
#include "jwt.h"
#include "syn_bar_notify.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define PING_EVERY_MS 25000
#define DEAD_AFTER_MS 70000     /* Zello pings every 30 s; $SYN_CAST_DEAD_AFTER_MS for tests */
#define LIVE_PREBUFFER_PACKETS 3
#define LIVE_PREBUFFER_MS 400
#define FEED_AHEAD (AUDIO_RATE / 2)
#define HELD_MAX 50
#define SAY_LEAD_MS 300
#define RECENT_STREAMS 32
#define KICK_WINDOW_MS 120000

app_t app;

/* stream_id -> history message, for on_transcription after the stream ended */
static struct {
	uint32_t stream_id;
	int channel;
	uint64_t msg_id;
} recent[RECENT_STREAMS];
static int recent_next;

static void tone(const char *meaning) {
	if (app.cfg.tones) {
		syn_bar_notify_meaning(meaning);
	}
}

static void notify(const char *title, const char *body) {
	if (!app.cfg.notify || app.focused) {
		return;
	}
	char *argv[] = {"notify-send", "-a", "syn-cast", (char *)title, (char *)body, NULL};
	spawn_detached(argv);
}

void app_toast(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(app.toast, sizeof app.toast, fmt, ap);
	va_end(ap);
	app.toast_until = now_ms() + 5000;
	app.dirty = true;
	LOG("%s", app.toast);
}

static const char *me(void) {
	return app.cfg.username[0] ? app.cfg.username : "you";
}

/* ---------- packet lists ---------- */

static void pl_push(pkt_list *l, const uint8_t *d, size_t n) {
	if (l->len + n > l->cap) {
		size_t nc = l->cap ? l->cap * 2 : 16384;
		while (nc < l->len + n) nc *= 2;
		l->buf = realloc(l->buf, nc);
		l->cap = nc;
	}
	if (l->n == l->ncap) {
		l->ncap = l->ncap ? l->ncap * 2 : 256;
		l->off = realloc(l->off, l->ncap * sizeof *l->off);
		l->plen = realloc(l->plen, l->ncap * sizeof *l->plen);
	}
	if (n) {
		memcpy(l->buf + l->len, d, n);
	}
	l->off[l->n] = (uint32_t)l->len;
	l->plen[l->n] = (uint16_t)n;
	l->n++;
	l->len += n;
}

static void pl_free(pkt_list *l) {
	free(l->buf);
	free(l->off);
	free(l->plen);
	memset(l, 0, sizeof *l);
}

/* ---------- history helpers ---------- */

static hmsg *msg(int channel, uint64_t id) {
	if (channel < 0 || channel >= app.n_chan) {
		return NULL;
	}
	return history_find(&app.chan[channel].hist, id);
}

static void add_event(int channel, const char *fmt, ...) {
	char text[300];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	hmsg m = {.kind = HM_EVENT, .text = text};
	history_add(&app.chan[channel].hist, &m);
	app.dirty = true;
}

/* ---------- sessions ---------- */

static void session_kill(int s, const char *why) {
	if (!app.sess[s].kill) {
		app.sess[s].kill = true;
		str_copy(app.sess[s].kill_why, sizeof app.sess[s].kill_why, why);
	}
}

static bool session_send(int s, char *json) {
	session *ss = &app.sess[s];
	bool ok = json && ss->conn && !ss->kill && ws_send(ss->conn, WS_TEXT, json, strlen(json), NULL, 0) == 0;
	if (!ok && ss->conn) {
		session_kill(s, "send failed");
	}
	free(json);
	return ok;
}

static bool session_send_bin(int s, const uint8_t hdr[ZP_BIN_HDR], const uint8_t *d, size_t n) {
	session *ss = &app.sess[s];
	bool ok = ss->conn && !ss->kill && ws_send(ss->conn, WS_BINARY, hdr, ZP_BIN_HDR, d, n) == 0;
	if (!ok && ss->conn) {
		session_kill(s, "send failed");
	}
	return ok;
}

static int next_seq(int s) {
	return ++app.sess[s].seq;
}

static void add_pending(int s, int seq, pend_kind k, int channel, uint64_t ref) {
	pending *slot = &app.sess[s].pend[0];
	for (int i = 0; i < MAX_PENDING; i++) {
		pending *p = &app.sess[s].pend[i];
		if (p->kind == P_NONE) {
			slot = p;
			break;
		}
		if (p->when < slot->when) {
			slot = p; /* all taken: the oldest reply is long overdue */
		}
	}
	*slot = (pending){seq, k, channel, ref, now_ms()};
}

static bool take_pending(int s, int seq, pending *out) {
	for (int i = 0; i < MAX_PENDING; i++) {
		pending *p = &app.sess[s].pend[i];
		if (p->kind != P_NONE && p->seq == seq) {
			*out = *p;
			p->kind = P_NONE;
			return true;
		}
	}
	return false;
}

static void schedule_retry(int s, int min_ms) {
	session *ss = &app.sess[s];
	ss->state = SS_RETRY;
	ss->backoff_ms = ss->backoff_ms ? ss->backoff_ms * 2 : 1000;
	if (ss->backoff_ms > 30000) {
		ss->backoff_ms = 30000;
	}
	int wait = ss->backoff_ms > min_ms ? ss->backoff_ms : min_ms;
	ss->retry_at = now_ms() + wait + rand() % 500;
	app.dirty = true;
}

static void session_connect(int s) {
	session *ss = &app.sess[s];
	ss->state = SS_CONNECTING;
	ss->kill = false;
	ss->cookie = ((uint64_t)s << 48) | ++app.next_cookie;
	if (ws_connect_async(app.cfg.server, app.connect_pipe[1], ss->cookie) != 0) {
		str_copy(ss->error, sizeof ss->error, "cannot start a connect thread");
		schedule_retry(s, 1000);
	}
	app.dirty = true;
}

static void send_logon(int s) {
	session *ss = &app.sess[s];
	char *token = NULL;
	char err[200] = "";
	bool use_refresh = ss->refresh_token[0] != 0;
	if (!use_refresh) {
		if (app.cfg.auth_token[0]) {
			token = strdup(app.cfg.auth_token);
		} else if (app.cfg.issuer[0] && app.cfg.key_path[0]) {
			token = jwt_mint(app.cfg.key_path, app.cfg.issuer, 120, err, sizeof err);
			if (!token) {
				str_copy(ss->error, sizeof ss->error, err);
				session_kill(s, err);
				return;
			}
		}
	}
	const char *names[CFG_MAX_CHANNELS];
	for (int k = 0; k < ss->n_chans; k++) {
		names[k] = app.chan[ss->chans[k]].name;
	}
	int seq = next_seq(s);
	char *json = zp_logon(seq, token, use_refresh ? ss->refresh_token : NULL, app.cfg.username,
		app.cfg.password, names, (size_t)ss->n_chans, false, app.cfg.transcriptions);
	free(token);
	add_pending(s, seq, P_LOGON, -1, use_refresh);
	session_send(s, json);
	ss->state = SS_LOGON;
}

static void rx_end(int i);
static void tx_abort(const char *why);

static void fail_pending(session *ss) {
	for (int i = 0; i < MAX_PENDING; i++) {
		pending *p = &ss->pend[i];
		if ((p->kind == P_TEXT || p->kind == P_LOCATION) && p->channel >= 0) {
			hmsg *m = msg(p->channel, p->ref);
			if (m && m->partial) {
				m->partial = false;
				history_commit(&app.chan[p->channel].hist, m);
				add_event(p->channel, "not delivered (connection lost)");
			}
		}
		if (p->kind == P_IMAGE) {
			app.img_out.busy = false;
		}
		p->kind = P_NONE;
	}
}

static void session_drop(int s, const char *why) {
	session *ss = &app.sess[s];
	bool was_online = ss->state == SS_ONLINE;
	if (ss->conn) {
		ws_close(ss->conn);
		ss->conn = NULL;
	}
	ss->kill = false;
	if (why && *why) {
		str_copy(ss->error, sizeof ss->error, why);
	}
	fail_pending(ss);
	for (int k = 0; k < ss->n_chans; k++) {
		channel *c = &app.chan[ss->chans[k]];
		c->online = false;
		c->users_online = 0;
		if (was_online) {
			add_event(ss->chans[k], "connection lost: %s", ss->error);
		}
		if (app.tx.phase != TX_IDLE && app.tx.channel == ss->chans[k]) {
			tx_abort("connection lost");
		}
	}
	for (int i = 0; i < MAX_RX; i++) {
		if (app.rx[i].used && app.rx[i].session == s) {
			rx_end(i);
		}
	}
	if (was_online) {
		tone("TOGGLE_OFF");
		ss->backoff_ms = 0;
	}
	LOG("session %d dropped: %s", s, ss->error);
	if (ss->kicked) {
		ss->kicked = false;
		int64_t now = now_ms();
		if (ss->last_kick && now - ss->last_kick < KICK_WINDOW_MS) {
			/* kicked twice in a row: another client keeps taking it, so give way */
			ss->last_kick = 0;
			ss->state = SS_OFF;
			app.offline = true;
			snprintf(ss->error, sizeof ss->error, "another client keeps signing in as %.60s", app.cfg.username);
			for (int k = 0; k < ss->n_chans; k++) {
				add_event(ss->chans[k], "signed in elsewhere: went offline");
			}
			app_toast("another client is using %s: SYN-CAST went offline (F6 brings it back)", app.cfg.username);
			return;
		}
		ss->last_kick = now;
		ss->backoff_ms = 0;
		LOG("another client signed in as %s: taking the account back", app.cfg.username);
	}
	if (ss->refused) {
		ss->state = SS_OFF; /* until the account is fixed (syn-cast setup) and it restarts */
		app.dirty = true;
		return;
	}
	/* a refused refresh token is retried with a fresh auth token at once */
	schedule_retry(s, 0);
}

void app_on_connect_pipe(void) {
	ws_connect_result *r;
	while (read(app.connect_pipe[0], &r, sizeof r) == (ssize_t)sizeof r) {
		int s = -1;
		for (int k = 0; k < app.n_sess; k++) {
			if (app.sess[k].cookie == r->cookie && app.sess[k].state == SS_CONNECTING) {
				s = k;
			}
		}
		if (s < 0) {
			ws_close(r->conn);
		} else if (!r->conn) {
			str_copy(app.sess[s].error, sizeof app.sess[s].error, r->err);
			LOG("connect: %s", r->err);
			schedule_retry(s, 0);
		} else {
			app.sess[s].conn = r->conn;
			app.sess[s].last_rx = app.sess[s].last_ping = now_ms();
			send_logon(s);
		}
		free(r);
		app.dirty = true;
	}
}

static int channel_in_session(int s, const char *name) {
	session *ss = &app.sess[s];
	for (int k = 0; k < ss->n_chans; k++) {
		if (strcasecmp(app.chan[ss->chans[k]].name, name) == 0) {
			return ss->chans[k];
		}
	}
	return ss->n_chans == 1 ? ss->chans[0] : -1;
}

int app_find_channel(const char *name) {
	for (int k = 0; k < app.n_chan; k++) {
		if (strcasecmp(app.chan[k].name, name) == 0) {
			return k;
		}
	}
	return -1;
}

/* ---------- incoming voice ---------- */

static void remember_stream(uint32_t sid, int channel, uint64_t id) {
	recent[recent_next].stream_id = sid;
	recent[recent_next].channel = channel;
	recent[recent_next].msg_id = id;
	recent_next = (recent_next + 1) % RECENT_STREAMS;
}

static void rx_free(int i) {
	rx_stream *r = &app.rx[i];
	hv_close(&r->file);
	pl_free(&r->pkts);
	memset(r, 0, sizeof *r);
}

static void player_push(play_item *it) {
	if (app.qn == MAX_QUEUE) {
		app_toast("playback queue full");
		if (it->kind == PI_REPLAY) {
			hv_free(&it->file);
		}
		return;
	}
	app.queue[app.qn++] = *it;
	app.dirty = true;
}

static void rx_start(int s, const zevent *ev) {
	int ch = channel_in_session(s, ev->channel);
	if (ch < 0) {
		return;
	}
	int slot = -1;
	for (int i = 0; i < MAX_RX; i++) {
		if (!app.rx[i].used) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		/* reuse the oldest finished stream that nothing is playing */
		int64_t oldest = INT64_MAX;
		for (int i = 0; i < MAX_RX; i++) {
			if (app.rx[i].ended && !app.rx[i].queued && app.rx[i].started < oldest) {
				oldest = app.rx[i].started;
				slot = i;
			}
		}
		if (slot < 0) {
			LOG("too many streams at once; dropping one from %s", ev->from);
			return;
		}
		rx_free(slot);
	}
	rx_stream *r = &app.rx[slot];
	r->used = true;
	r->session = s;
	r->channel = ch;
	r->stream_id = ev->stream_id;
	str_copy(r->from, sizeof r->from, ev->from);
	str_copy(r->forwho, sizeof r->forwho, ev->forwho);
	r->codec = ev->codec_hdr;
	r->started = now_ms();

	channel *c = &app.chan[ch];
	hmsg m = {.kind = HM_VOICE, .partial = true};
	str_copy(m.from, sizeof m.from, ev->from);
	str_copy(m.forwho, sizeof m.forwho, ev->forwho);
	r->msg_id = history_add(&c->hist, &m);
	char path[1200];
	history_voice_path(&c->hist, r->msg_id, path, sizeof path);
	if (hv_open(&r->file, path, &r->codec) != 0) {
		LOG("cannot write %s", path);
	}
	remember_stream(r->stream_id, ch, r->msg_id);

	if (app.cfg.auto_play && !app.busy && !c->muted) {
		play_item it = {.kind = PI_LIVE, .rx = slot, .channel = ch, .msg_id = r->msg_id, .began = now_ms()};
		str_copy(it.from, sizeof it.from, ev->from);
		r->queued = true;
		player_push(&it);
	} else {
		c->unread++;
		if (app.busy && app.busy_qn < BUSY_Q_MAX) {
			app.busy_q[app.busy_qn].channel = ch;
			app.busy_q[app.busy_qn].msg_id = r->msg_id;
			app.busy_qn++;
		}
	}
	LOG("%s: voice from %s%s%s", c->name, ev->from, ev->forwho[0] ? " for " : "", ev->forwho);
	app.dirty = true;
}

static void rx_finalize(rx_stream *r) {
	hmsg *m = msg(r->channel, r->msg_id);
	int dur = hv_duration_ms(&r->file);
	hv_close(&r->file);
	if (m) {
		m->partial = false;
		m->duration_ms = dur;
		history_commit(&app.chan[r->channel].hist, m);
	}
}

static void rx_end(int i) {
	rx_stream *r = &app.rx[i];
	if (!r->used || r->ended) {
		return;
	}
	r->ended = true;
	rx_finalize(r);
	if (!r->queued) {
		rx_free(i);
	}
	app.dirty = true;
}

static void rx_packet(int s, const zp_bin *b) {
	for (int i = 0; i < MAX_RX; i++) {
		rx_stream *r = &app.rx[i];
		if (!r->used || r->ended || r->session != s || r->stream_id != b->id) {
			continue;
		}
		if (r->have_pid && b->sub > r->last_pid + 1 && b->sub - r->last_pid <= 25) {
			/* lost packets: marked so playback can cover them with FEC/PLC */
			for (uint32_t k = r->last_pid + 1; k < b->sub; k++) {
				pl_push(&r->pkts, NULL, 0);
				hv_write(&r->file, NULL, 0);
			}
		}
		r->last_pid = b->sub;
		r->have_pid = true;
		pl_push(&r->pkts, b->data, b->len);
		hv_write(&r->file, b->data, b->len);
		return;
	}
}

/* ---------- player ---------- */

static void finish_item(bool natural) {
	if (!app.playing || app.qn == 0) {
		return;
	}
	play_item *it = &app.queue[0];
	hmsg *m = msg(it->channel, it->msg_id);
	if (m && !m->played) {
		m->played = true;
		history_commit(&app.chan[it->channel].hist, m);
	}
	if (natural && it->kind == PI_LIVE) {
		tone("STOP"); /* the roger beep: that message is over */
	}
	rxdec_free(&it->dec);
	stretch_free(it->st);
	if (it->kind == PI_REPLAY) {
		hv_free(&it->file);
	} else {
		rx_stream *r = &app.rx[it->rx];
		r->queued = false;
		if (r->ended) {
			rx_free(it->rx);
		}
	}
	memmove(app.queue, app.queue + 1, sizeof app.queue[0] * (size_t)(app.qn - 1));
	app.qn--;
	app.playing = false;
	app.dirty = true;
}

static void feed(play_item *it) {
	static int16_t pcm[MAX_PACKET_SAMPLES * 22];
	static int16_t out[MAX_PACKET_SAMPLES * 44];
	size_t n;
	bool ended;
	rx_stream *r = NULL;
	if (it->kind == PI_LIVE) {
		r = &app.rx[it->rx];
		n = r->pkts.n;
		ended = r->ended;
	} else {
		n = it->file.n;
		ended = true;
	}
	if (!it->started) {
		if (it->kind == PI_LIVE && !ended && n - it->cursor < LIVE_PREBUFFER_PACKETS &&
			now_ms() - it->began < LIVE_PREBUFFER_MS) {
			return;
		}
		it->started = true;
	}
	while (playback_queued() < FEED_AHEAD && it->cursor < n) {
		const uint8_t *d;
		size_t len;
		if (r) {
			d = r->pkts.buf + r->pkts.off[it->cursor];
			len = r->pkts.plen[it->cursor];
		} else {
			d = it->file.p[it->cursor].data;
			len = it->file.p[it->cursor].len;
		}
		it->cursor++;
		if (len == 0) {
			rxdec_lost(&it->dec, 1);
			continue;
		}
		int got = rxdec_decode(&it->dec, d, len, pcm, ARRAY_LEN(pcm));
		if (got > 0) {
			size_t m = stretch_process(it->st, pcm, (size_t)got, out, ARRAY_LEN(out));
			playback_write(out, m);
		}
	}
	if (it->cursor >= n && ended) {
		size_t m;
		while ((m = stretch_flush(it->st, out, ARRAY_LEN(out))) > 0) {
			playback_write(out, m);
		}
		it->fed_all = true;
		it->end_mark = playback_written();
	}
}

static void player_tick(void) {
	if (!app.playing) {
		if (app.qn == 0 || app.tx.phase != TX_IDLE) {
			return;
		}
		play_item *it = &app.queue[0];
		if (rxdec_init(&it->dec) != 0) {
			LOG("opus decoder unavailable");
			app.playing = true;
			finish_item(false);
			return;
		}
		it->st = stretch_new();
		stretch_reset(it->st, it->kind == PI_REPLAY ? app.speed : 1.0);
		it->began = now_ms();
		app.playing = true;
		app.dirty = true;
	}
	play_item *it = &app.queue[0];
	if (!it->fed_all) {
		feed(it);
	}
	if (it->fed_all && playback_played() >= it->end_mark) {
		finish_item(true);
	}
}

void app_stop_playback(void) {
	if (app.playing) {
		playback_flush();
		finish_item(false);
	}
}

void app_replay(int channel, uint64_t msg_id) {
	hmsg *m = msg(channel, msg_id);
	if (!m || m->kind != HM_VOICE || m->partial) {
		return;
	}
	play_item it = {.kind = PI_REPLAY, .channel = channel, .msg_id = msg_id};
	str_copy(it.from, sizeof it.from, m->from);
	it.duration_ms = m->duration_ms;
	char path[1200];
	history_voice_path(&app.chan[channel].hist, msg_id, path, sizeof path);
	if (hv_load(path, &it.file) != 0) {
		app_toast("cannot read that voice message");
		return;
	}
	if (app.playing && app.queue[0].kind == PI_REPLAY) {
		app_stop_playback(); /* a new replay replaces the one on air */
	}
	if (app.qn == MAX_QUEUE) {
		hv_free(&it.file);
		return;
	}
	/* replays go ahead of anything waiting, after whatever is on air */
	int at = app.playing ? 1 : 0;
	memmove(app.queue + at + 1, app.queue + at, sizeof app.queue[0] * (size_t)(app.qn - at));
	app.queue[at] = it;
	app.qn++;
	app.dirty = true;
}

void app_replay_last(void) {
	history *h = &app.chan[app.active].hist;
	for (size_t k = h->n; k-- > 0;) {
		if (h->v[k].kind == HM_VOICE && !h->v[k].out && !h->v[k].partial) {
			app_replay(app.active, h->v[k].id);
			return;
		}
	}
	app_toast("no message to replay");
}

const char *app_rx_name(int *level, int *elapsed_ms) {
	if (!app.playing) {
		*level = 0;
		*elapsed_ms = 0;
		return "";
	}
	*level = playback_level();
	*elapsed_ms = (int)(now_ms() - app.queue[0].began);
	return app.queue[0].from;
}

/* ---------- talking ---------- */

talk_state app_talk_state(void) {
	if (app.tx.phase == TX_STARTING) return TB_CONNECTING;
	if (app.tx.phase == TX_SENDING) return TB_SENDING;
	if (app.playing) return TB_RECEIVING;
	if (app.n_chan == 0 || !app.chan[app.active].online) return TB_DISABLED;
	return TB_IDLE;
}

static void tx_reset(void) {
	pl_free(&app.tx.held);
	hv_close(&app.tx.file);
	free(app.tx.say_pcm);
	if (app.tx.say_enc) {
		opus_encoder_destroy(app.tx.say_enc);
	}
	memset(&app.tx, 0, sizeof app.tx);
	app.dirty = true;
}

static bool tx_begin(bool vox) {
	if (app.n_chan == 0) {
		return false;
	}
	channel *c = &app.chan[app.active];
	if (app.tx.phase != TX_IDLE) {
		return false;
	}
	if (!c->online) {
		tone("FAIL");
		app_toast("%s is offline", c->name);
		return false;
	}
	if (app.playing && app.queue[0].kind == PI_REPLAY) {
		app_stop_playback();
	}
	int s = c->session;
	int seq = next_seq(s);
	if (!session_send(s, zp_start_stream(seq, c->name, NULL, &PTT_CODEC))) {
		tone("FAIL");
		app_toast("not connected");
		return false;
	}
	add_pending(s, seq, P_START, app.active, 0);
	app.tx.phase = TX_STARTING;
	app.tx.channel = app.active;
	app.tx.start_seq = seq;
	app.tx.started = now_ms();
	app.tx.vox = vox;
	app.dirty = true;
	return true;
}

static void tx_send_packet(const uint8_t *d, size_t n) {
	channel *c = &app.chan[app.tx.channel];
	uint8_t hdr[ZP_BIN_HDR];
	zp_bin_header(hdr, ZP_BIN_AUDIO, app.tx.stream_id, 0);
	session_send_bin(c->session, hdr, d, n);
	hv_write(&app.tx.file, d, n);
}

static void tx_finish(void) {
	channel *c = &app.chan[app.tx.channel];
	if (app.tx.phase == TX_SENDING) {
		int seq = next_seq(c->session);
		/* the reference gives stop_stream no reply shape: nothing to wait for */
		session_send(c->session, zp_stop_stream(seq, c->name, app.tx.stream_id));
		hmsg *m = msg(app.tx.channel, app.tx.msg_id);
		if (m) {
			m->partial = false;
			m->duration_ms = hv_duration_ms(&app.tx.file);
			history_commit(&c->hist, m);
		}
	}
	tx_reset();
}

static void tx_abort(const char *why) {
	if (app.tx.phase == TX_IDLE) {
		return;
	}
	if (!app.tx.say_pcm && !app.tx.released && !app.tx.vox) {
		capture_end();
	}
	if (why) {
		app_toast("talk ended: %s", why);
	}
	tx_finish();
}

static void tx_packet(const uint8_t *d, size_t n) {
	if (app.tx.phase == TX_STARTING) {
		if (app.tx.held.n < HELD_MAX) {
			pl_push(&app.tx.held, d, n);
		}
	} else if (app.tx.phase == TX_SENDING) {
		tx_send_packet(d, n);
	}
}

static void tx_input_ended(void) {
	if (app.tx.phase == TX_IDLE) {
		return;
	}
	app.tx.end_seen = true;
	if (app.tx.phase == TX_SENDING) {
		tx_finish();
	}
	/* TX_STARTING: finished when the server answers start_stream */
}

static void tx_started(int s, const zevent *ev) {
	if (app.tx.phase != TX_STARTING || ev->seq != app.tx.start_seq) {
		if (ev->success && ev->stream_id) {
			/* the button was given up on before the server answered */
			int ch = app.tx.phase == TX_IDLE ? app.active : app.tx.channel;
			session_send(s, zp_stop_stream(next_seq(s), app.chan[ch].name, ev->stream_id));
		}
		return;
	}
	if (!ev->success) {
		tone("FAIL");
		app_toast("cannot talk: %s", ev->error[0] ? ev->error : "refused");
		if (!app.tx.say_pcm && !app.tx.released && !app.tx.vox) {
			capture_end();
		}
		tx_reset();
		return;
	}
	channel *c = &app.chan[app.tx.channel];
	app.tx.stream_id = ev->stream_id;
	app.tx.phase = TX_SENDING;
	tone("START"); /* talk permit */
	hmsg m = {.kind = HM_VOICE, .out = true, .played = true, .partial = true};
	str_copy(m.from, sizeof m.from, me());
	app.tx.msg_id = history_add(&c->hist, &m);
	char path[1200];
	history_voice_path(&c->hist, app.tx.msg_id, path, sizeof path);
	hv_open(&app.tx.file, path, &PTT_CODEC);
	for (size_t k = 0; k < app.tx.held.n; k++) {
		tx_send_packet(app.tx.held.buf + app.tx.held.off[k], app.tx.held.plen[k]);
	}
	pl_free(&app.tx.held);
	if (app.tx.end_seen) {
		tx_finish();
	}
	app.dirty = true;
}

void app_ptt_down(void) {
	if (app.tx.phase != TX_IDLE) {
		if (app.tx.vox && !app.tx.say_pcm) {
			app.tx.vox = false; /* the button takes over a VOX transmission */
			capture_begin();
		}
		return;
	}
	if (tx_begin(false)) {
		capture_begin();
	}
}

void app_ptt_up(void) {
	if (app.tx.phase == TX_IDLE || app.tx.say_pcm || app.tx.vox || app.tx.released) {
		return;
	}
	app.tx.released = true;
	capture_end();
}

void app_ptt_toggle(void) {
	if (app.tx.phase == TX_IDLE) {
		app_ptt_down();
	} else {
		app_ptt_up();
	}
}

int app_say_file(const char *wav_path, char *err, size_t errn) {
	if (app.tx.phase != TX_IDLE) {
		snprintf(err, errn, "already talking");
		return -1;
	}
	int16_t *pcm;
	size_t n;
	if (wav_load(wav_path, &pcm, &n, err, errn) != 0) {
		return -1;
	}
	if (!tx_begin(false)) {
		free(pcm);
		snprintf(err, errn, "%s", app.toast[0] ? app.toast : "cannot talk now");
		return -1;
	}
	app.tx.say_pcm = pcm;
	app.tx.say_n = n;
	app.tx.say_enc = ptt_encoder_create();
	app.tx.say_t0 = now_ms();
	return 0;
}

static void say_tick(void) {
	tx_state *t = &app.tx;
	if (!t->say_pcm || t->end_seen) {
		return;
	}
	int64_t due = (now_ms() - t->say_t0 + SAY_LEAD_MS) / FRAME_MS + 1;
	int16_t frame[FRAME_SAMPLES];
	uint8_t pkt[MAX_OPUS_PACKET];
	while (t->say_sent < due && t->say_pos < t->say_n) {
		size_t take = t->say_n - t->say_pos < FRAME_SAMPLES ? t->say_n - t->say_pos : FRAME_SAMPLES;
		memcpy(frame, t->say_pcm + t->say_pos, take * sizeof *frame);
		memset(frame + take, 0, (FRAME_SAMPLES - take) * sizeof *frame);
		t->say_pos += take;
		int n = opus_encode(t->say_enc, frame, FRAME_SAMPLES, pkt, sizeof pkt);
		if (n > 0) {
			t->level = 60;
			tx_packet(pkt, (size_t)n);
		}
		t->say_sent++;
	}
	if (t->say_pos >= t->say_n) {
		tx_input_ended();
	}
}

void app_on_capture_pipe(void) {
	for (;;) {
		cap_hdr h;
		ssize_t r = read(app.capture_pipe[0], &h, sizeof h);
		if (r != (ssize_t)sizeof h) {
			return;
		}
		uint8_t data[MAX_OPUS_PACKET];
		if (h.len && read(app.capture_pipe[0], data, h.len) != h.len) {
			return;
		}
		switch (h.kind) {
		case CAP_PACKET:
			app.tx.level = h.level;
			tx_packet(data, h.len);
			break;
		case CAP_END:
			tx_input_ended();
			break;
		case CAP_VOX_START:
			if (app.tx.phase == TX_IDLE) {
				tx_begin(true);
			}
			break;
		case CAP_ERROR:
			tone("FAIL");
			app_toast("microphone unavailable");
			if (app.tx.phase != TX_IDLE && !app.tx.say_pcm) {
				app.tx.released = true;
				tx_abort(NULL);
			}
			break;
		}
		app.dirty = true;
	}
}

/* ---------- text, images, locations ---------- */

static channel *ready_channel(char *err, size_t errn) {
	if (app.n_chan == 0) {
		snprintf(err, errn, "no channel configured");
		return NULL;
	}
	channel *c = &app.chan[app.active];
	if (!c->online) {
		snprintf(err, errn, "%s is offline", c->name);
		return NULL;
	}
	return c;
}

int app_send_text(const char *text, char *err, size_t errn) {
	channel *c = ready_channel(err, errn);
	if (!c) {
		return -1;
	}
	if (!*text) {
		snprintf(err, errn, "empty message");
		return -1;
	}
	if (strlen(text) > 30000) {
		snprintf(err, errn, "text longer than the 30 KB the API takes");
		return -1;
	}
	int seq = next_seq(c->session);
	if (!session_send(c->session, zp_send_text(seq, c->name, NULL, text))) {
		snprintf(err, errn, "not connected");
		return -1;
	}
	hmsg m = {.kind = HM_TEXT, .out = true, .partial = true, .text = (char *)text};
	str_copy(m.from, sizeof m.from, me());
	uint64_t id = history_add(&c->hist, &m);
	add_pending(c->session, seq, P_TEXT, app.active, id);
	app.dirty = true;
	return 0;
}

int app_send_location(double lat, double lon, const char *address, char *err, size_t errn) {
	channel *c = ready_channel(err, errn);
	if (!c) {
		return -1;
	}
	if (lat < -90 || lat > 90 || lon < -180 || lon > 180) {
		snprintf(err, errn, "latitude -90..90, longitude -180..180");
		return -1;
	}
	int seq = next_seq(c->session);
	if (!session_send(c->session, zp_send_location(seq, c->name, NULL, lat, lon, 0, address))) {
		snprintf(err, errn, "not connected");
		return -1;
	}
	hmsg m = {.kind = HM_LOCATION, .out = true, .partial = true, .lat = lat, .lon = lon,
		.text = (char *)(address ? address : "")};
	str_copy(m.from, sizeof m.from, me());
	uint64_t id = history_add(&c->hist, &m);
	add_pending(c->session, seq, P_LOCATION, app.active, id);
	app.dirty = true;
	return 0;
}

int app_send_image(const char *jpeg_path, char *err, size_t errn) {
	channel *c = ready_channel(err, errn);
	if (!c) {
		return -1;
	}
	if (app.img_out.busy) {
		snprintf(err, errn, "an image is still being sent");
		return -1;
	}
	uint8_t *full, *thumb;
	size_t full_n, thumb_n;
	int w, h;
	if (image_prepare(jpeg_path, &full, &full_n, &thumb, &thumb_n, &w, &h, err, errn) != 0) {
		return -1;
	}
	int seq = next_seq(c->session);
	if (!session_send(c->session, zp_send_image(seq, c->name, NULL, w, h, thumb_n, full_n))) {
		free(full);
		free(thumb);
		snprintf(err, errn, "not connected");
		return -1;
	}
	app.img_out.full = full;
	app.img_out.full_n = full_n;
	app.img_out.thumb = thumb;
	app.img_out.thumb_n = thumb_n;
	app.img_out.w = w;
	app.img_out.h = h;
	app.img_out.channel = app.active;
	app.img_out.seq = seq;
	app.img_out.busy = true;
	add_pending(c->session, seq, P_IMAGE, app.active, 0);
	return 0;
}

static void image_sent(int s, const zevent *ev) {
	int ch = app.img_out.channel;
	if (ev->success && ev->image_id) {
		uint8_t hdr[ZP_BIN_HDR];
		zp_bin_header(hdr, ZP_BIN_IMAGE, ev->image_id, ZP_IMAGE_THUMB);
		session_send_bin(s, hdr, app.img_out.thumb, app.img_out.thumb_n);
		zp_bin_header(hdr, ZP_BIN_IMAGE, ev->image_id, ZP_IMAGE_FULL);
		session_send_bin(s, hdr, app.img_out.full, app.img_out.full_n);
		hmsg m = {.kind = HM_IMAGE, .out = true, .w = app.img_out.w, .h = app.img_out.h};
		str_copy(m.from, sizeof m.from, me());
		history *hist = &app.chan[ch].hist;
		uint64_t id = history_add(hist, &m);
		char p[1200];
		history_image_path(hist, id, true, p, sizeof p);
		write_file(p, app.img_out.thumb, app.img_out.thumb_n, 0600);
		history_image_path(hist, id, false, p, sizeof p);
		write_file(p, app.img_out.full, app.img_out.full_n, 0600);
	} else {
		app_toast("image not sent: %s", ev->error[0] ? ev->error : "refused");
	}
	free(app.img_out.full);
	free(app.img_out.thumb);
	app.img_out.full = app.img_out.thumb = NULL;
	app.img_out.busy = false;
	app.dirty = true;
}

static void image_in_start(int ch, const zevent *ev) {
	hmsg m = {.kind = HM_IMAGE, .partial = true, .w = ev->width, .h = ev->height};
	str_copy(m.from, sizeof m.from, ev->from);
	str_copy(m.forwho, sizeof m.forwho, ev->forwho);
	uint64_t id = history_add(&app.chan[ch].hist, &m);
	int slot = 0;
	for (int i = 0; i < MAX_IMAGES_IN; i++) {
		if (!app.images[i].msg_id) {
			slot = i;
			break;
		}
		if (app.images[i].when < app.images[slot].when) {
			slot = i;
		}
	}
	app.images[slot] = (image_in){ev->message_id, ch, id, now_ms()};
}

static void image_in_data(const zp_bin *b) {
	for (int i = 0; i < MAX_IMAGES_IN; i++) {
		image_in *in = &app.images[i];
		if (!in->msg_id || in->message_id != b->id) {
			continue;
		}
		history *h = &app.chan[in->channel].hist;
		char p[1200];
		history_image_path(h, in->msg_id, b->sub == ZP_IMAGE_THUMB, p, sizeof p);
		write_file(p, b->data, b->len, 0600);
		if (b->sub == ZP_IMAGE_FULL) {
			hmsg *m = history_find(h, in->msg_id);
			if (m) {
				m->partial = false;
				history_commit(h, m);
				char line[200];
				snprintf(line, sizeof line, "%s sent an image", m->from);
				notify(app.chan[in->channel].name, line);
			}
			tone("ALERT");
			memset(in, 0, sizeof *in);
		}
		app.dirty = true;
		return;
	}
}

/* ---------- frames ---------- */

static void on_reply(int s, const zevent *ev) {
	pending p;
	if (!take_pending(s, ev->seq, &p)) {
		return;
	}
	session *ss = &app.sess[s];
	switch (p.kind) {
	case P_LOGON:
		if (ev->success) {
			ss->state = SS_ONLINE;
			ss->backoff_ms = 0;
			ss->error[0] = 0;
			if (ev->refresh_token[0]) {
				str_copy(ss->refresh_token, sizeof ss->refresh_token, ev->refresh_token);
			}
			LOG("signed in as %s", app.cfg.username[0] ? app.cfg.username : "(anonymous)");
		} else {
			char why[200];
			snprintf(why, sizeof why, "sign-in refused: %s", ev->error[0] ? ev->error : "unknown");
			if (p.ref) {
				ss->refresh_token[0] = 0; /* stale: the next try mints a token */
			} else {
				/* a wrong password, username or key: retrying only risks a lockout */
				ss->refused = true;
			}
			session_kill(s, why);
		}
		break;
	case P_START:
		tx_started(s, ev);
		break;
	case P_TEXT:
	case P_LOCATION: {
		hmsg *m = msg(p.channel, p.ref);
		if (m) {
			m->partial = false;
			history_commit(&app.chan[p.channel].hist, m);
		}
		if (!ev->success) {
			add_event(p.channel, "not delivered: %s", ev->error[0] ? ev->error : "refused");
			tone("FAIL");
		}
		break;
	}
	case P_IMAGE:
		image_sent(s, ev);
		break;
	default:
		break;
	}
	app.dirty = true;
}

static void on_json(int s, const uint8_t *d, size_t n) {
	zevent ev;
	if (zp_parse((const char *)d, n, &ev) != 0) {
		return;
	}
	int ch = ev.channel[0] || app.sess[s].n_chans == 1 ? channel_in_session(s, ev.channel) : -1;
	switch (ev.kind) {
	case ZEV_REPLY:
		on_reply(s, &ev);
		break;
	case ZEV_CHANNEL_STATUS:
		if (ch >= 0) {
			channel *c = &app.chan[ch];
			bool was = c->online;
			c->online = ev.online;
			c->users_online = ev.users_online;
			c->images_ok = ev.images_ok;
			c->texting_ok = ev.texting_ok;
			c->locations_ok = ev.locations_ok;
			str_copy(c->error, sizeof c->error, ev.error);
			if (ev.online && !was) {
				add_event(ch, "connected, %d online", ev.users_online);
				if (app.sess[s].was_online) {
					tone("SUCCESS");
				}
				app.sess[s].was_online = true;
			} else if (!ev.online && was) {
				add_event(ch, "channel offline%s%s", ev.error[0] ? ": " : "", ev.error);
			}
		}
		break;
	case ZEV_STREAM_START:
		rx_start(s, &ev);
		break;
	case ZEV_STREAM_STOP:
		for (int i = 0; i < MAX_RX; i++) {
			if (app.rx[i].used && app.rx[i].session == s && app.rx[i].stream_id == ev.stream_id) {
				rx_end(i);
			}
		}
		break;
	case ZEV_TEXT:
		if (ch >= 0 && ev.text) {
			hmsg m = {.kind = HM_TEXT, .text = ev.text};
			str_copy(m.from, sizeof m.from, ev.from);
			str_copy(m.forwho, sizeof m.forwho, ev.forwho);
			history_add(&app.chan[ch].hist, &m);
			if (ch != app.active) {
				app.chan[ch].unread++;
			}
			if (!app.chan[ch].muted) {
				tone("ALERT");
			}
			char title[200];
			snprintf(title, sizeof title, "%s%s%s", ev.from, ev.forwho[0] ? " (private) in " : " in ",
				app.chan[ch].name);
			notify(title, ev.text);
		}
		break;
	case ZEV_IMAGE:
		if (ch >= 0) {
			image_in_start(ch, &ev);
		}
		break;
	case ZEV_LOCATION:
		if (ch >= 0) {
			hmsg m = {.kind = HM_LOCATION, .lat = ev.latitude, .lon = ev.longitude,
				.acc = ev.accuracy, .text = ev.text};
			str_copy(m.from, sizeof m.from, ev.from);
			str_copy(m.forwho, sizeof m.forwho, ev.forwho);
			history_add(&app.chan[ch].hist, &m);
			tone("ALERT");
		}
		break;
	case ZEV_TRANSCRIPTION:
		for (int k = 0; k < RECENT_STREAMS; k++) {
			if (recent[k].msg_id && recent[k].stream_id == ev.stream_id) {
				hmsg *m = msg(recent[k].channel, recent[k].msg_id);
				if (m && ev.text) {
					history_set_text(m, ev.text);
					history_commit(&app.chan[recent[k].channel].hist, m);
				}
			}
		}
		break;
	case ZEV_ERROR:
		app_toast("server: %s", ev.error);
		if (strcmp(ev.error, "server closed connection") == 0) {
			session_kill(s, ev.error);
		} else if (strcmp(ev.error, "kicked") == 0) {
			app.sess[s].kicked = true;
			session_kill(s, "signed in elsewhere");
		}
		break;
	default:
		break;
	}
	zp_event_free(&ev);
	app.dirty = true;
}

static void on_frame(void *ud, int op, const uint8_t *d, size_t n) {
	int s = (int)(intptr_t)ud;
	app.sess[s].last_rx = now_ms();
	if (op == WS_TEXT) {
		on_json(s, d, n);
	} else if (op == WS_BINARY) {
		zp_bin b;
		if (zp_parse_bin(d, n, &b) != 0) {
			return;
		}
		if (b.type == ZP_BIN_AUDIO) {
			rx_packet(s, &b);
		} else if (b.type == ZP_BIN_IMAGE) {
			image_in_data(&b);
		}
	}
}

int app_session_fds(int *fds, short *events, int *sess_idx, int max) {
	int n = 0;
	for (int s = 0; s < app.n_sess && n < max; s++) {
		if (app.sess[s].conn) {
			fds[n] = ws_fd(app.sess[s].conn);
			events[n] = POLLIN | (ws_wants_write(app.sess[s].conn) ? POLLOUT : 0);
			sess_idx[n] = s;
			n++;
		}
	}
	return n;
}

void app_on_session_io(int s, short revents) {
	session *ss = &app.sess[s];
	if (!ss->conn) {
		return;
	}
	if ((revents & POLLOUT) && ws_flush(ss->conn) < 0) {
		session_kill(s, "connection lost");
		return;
	}
	if (revents & (POLLIN | POLLHUP | POLLERR)) {
		if (ws_read(ss->conn, on_frame, (void *)(intptr_t)s) < 0) {
			if (ws_close_code(ss->conn) == 3003) {
				ss->kicked = true; /* Zello's "another client took this account" */
				session_kill(s, "signed in elsewhere");
			} else {
				session_kill(s, "connection closed by the server");
			}
		}
	}
}

/* ---------- selection, modes ---------- */

static bool may_connect(void) {
	session *ss = &app.sess[0];
	return !app.offline && !ss->refused && !config_is_bot_account(app.cfg.username) && config_has_account(&app.cfg);
}

/* Closes the session on purpose (a channel switch, going offline). */
static void leave(void) {
	session *ss = &app.sess[0];
	if (app.tx.phase != TX_IDLE) {
		tx_abort(NULL);
	}
	if (ss->conn) {
		ws_close(ss->conn);
		ss->conn = NULL;
	}
	fail_pending(ss);
	for (int i = 0; i < MAX_RX; i++) {
		if (app.rx[i].used) {
			rx_end(i);
		}
	}
	for (int k = 0; k < ss->n_chans; k++) {
		app.chan[ss->chans[k]].online = false;
		app.chan[ss->chans[k]].users_online = 0;
	}
	ss->kill = ss->kicked = false;
	ss->backoff_ms = 0;
	ss->state = SS_OFF;
	app.dirty = true;
}

void app_set_offline(bool off) {
	if (app.n_sess == 0 || off == app.offline) {
		return;
	}
	app.offline = off;
	session *ss = &app.sess[0];
	if (off) {
		leave();
		ss->error[0] = 0;
		app_toast("offline: the account is free for another client");
	} else {
		ss->error[0] = 0;
		ss->last_kick = 0;
		if (may_connect()) {
			session_connect(0);
		}
		app_toast("going online");
	}
}

/* Leaves the joined channel and joins idx on the same session. */
static void rejoin(int idx) {
	session *ss = &app.sess[0];
	leave();
	ss->chans[0] = idx;
	ss->n_chans = 1;
	ss->error[0] = 0;
	if (may_connect()) {
		session_connect(0);
	}
}

void app_select_channel(int idx) {
	if (idx < 0 || idx >= app.n_chan || app.tx.phase != TX_IDLE) {
		return;
	}
	if (idx != app.active) {
		rejoin(idx);
	}
	app.active = idx;
	app.chan[idx].unread = 0;
	app.dirty = true;
}

void app_cycle_speed(void) {
	app.speed = app.speed < 1.1 ? 1.25 : app.speed < 1.4 ? 1.5 : 1.0;
	app_toast("replay speed %.2gx", app.speed);
}

void app_toggle_busy(void) {
	app.busy = !app.busy;
	app_toast(app.busy ? "Busy: incoming voice is kept, not played" : "Available");
	if (!app.busy) {
		/* play the messages queued while busy, in order */
		for (int i = app.busy_qn - 1; i >= 0; i--) {
			hmsg *m = msg(app.busy_q[i].channel, app.busy_q[i].msg_id);
			if (m && !m->partial) {
				channel *c = &app.chan[app.busy_q[i].channel];
				if (c->unread > 0) c->unread--;
			}
			app_replay(app.busy_q[i].channel, app.busy_q[i].msg_id);
		}
		app.busy_qn = 0;
	}
	app.dirty = true;
}

void app_toggle_mute(void) {
	if (app.n_chan == 0) {
		return;
	}
	channel *c = &app.chan[app.active];
	c->muted = !c->muted;
	app_toast("%s %s", c->name, c->muted ? "muted" : "unmuted");
}

void app_toggle_vox(void) {
	bool on = !capture_vox();
	capture_set_vox(on);
	app.cfg.vox = on;
	app_toast(on ? "VOX on: talking opens the channel" : "VOX off");
}

const char *app_session_label(int ch) {
	static char buf[64];
	session *ss = &app.sess[app.chan[ch].session];
	bool joined = false;
	for (int k = 0; k < ss->n_chans; k++) {
		joined |= ss->chans[k] == ch;
	}
	if (!joined) {
		return "NOT JOINED";
	}
	switch (ss->state) {
	case SS_OFF:
		return app.offline ? "OFFLINE" : ss->refused ? "REFUSED" : "OFF";
	case SS_CONNECTING:
		return "CONNECTING";
	case SS_LOGON:
		return "SIGNING IN";
	case SS_ONLINE:
		return app.chan[ch].online ? "ONLINE" : "WAITING";
	case SS_RETRY: {
		int64_t left = (ss->retry_at - now_ms() + 999) / 1000;
		snprintf(buf, sizeof buf, "RETRY %llds", (long long)(left > 0 ? left : 0));
		return buf;
	}
	}
	return "";
}

/* ---------- typed commands ---------- */

const char *const APP_HELP =
	"/img FILE.jpg   "
	"/loc LAT LON [ADDRESS]   /say FILE.wav   /replay   /busy   /mute   /vox   /speed";

void app_input(const char *line) {
	char err[200] = "";
	while (*line == ' ') {
		line++;
	}
	if (line[0] != '/') {
		if (app_send_text(line, err, sizeof err) != 0) {
			app_toast("%s", err);
		}
		return;
	}
	char cmd[16] = "";
	const char *arg = line + 1;
	size_t k = 0;
	while (*arg && *arg != ' ' && k + 1 < sizeof cmd) {
		cmd[k++] = *arg++;
	}
	cmd[k] = 0;
	while (*arg == ' ') {
		arg++;
	}
	int rc = 0;
	if (strcmp(cmd, "img") == 0 || strcmp(cmd, "image") == 0) {
		char path[1024];
		rc = app_send_image(expand_home(arg, path, sizeof path), err, sizeof err);
	} else if (strcmp(cmd, "say") == 0) {
		char path[1024];
		rc = app_say_file(expand_home(arg, path, sizeof path), err, sizeof err);
	} else if (strcmp(cmd, "loc") == 0) {
		double lat, lon;
		int used = 0;
		if (sscanf(arg, "%lf %lf %n", &lat, &lon, &used) < 2) {
			snprintf(err, sizeof err, "usage: /loc LAT LON [ADDRESS]");
			rc = -1;
		} else {
			rc = app_send_location(lat, lon, arg + used, err, sizeof err);
		}
	} else if (strcmp(cmd, "replay") == 0) {
		app_replay_last();
	} else if (strcmp(cmd, "busy") == 0) {
		app_toggle_busy();
	} else if (strcmp(cmd, "mute") == 0) {
		app_toggle_mute();
	} else if (strcmp(cmd, "vox") == 0) {
		app_toggle_vox();
	} else if (strcmp(cmd, "speed") == 0) {
		app_cycle_speed();
	} else {
		app_toast("%s", APP_HELP);
	}
	if (rc != 0) {
		app_toast("%s", err);
	}
}

/* ---------- loop ---------- */

int app_timeout_ms(void) {
	if (app.tx.phase != TX_IDLE || app.playing || app.qn > 0) {
		return 20;
	}
	int64_t now = now_ms(), next = now + 1000;
	for (int s = 0; s < app.n_sess; s++) {
		if (app.sess[s].state == SS_RETRY && app.sess[s].retry_at < next) {
			next = app.sess[s].retry_at;
		}
	}
	if (app.toast_until > now && app.toast_until < next) {
		next = app.toast_until;
	}
	int64_t t = next - now;
	return t < 0 ? 0 : (int)t;
}

void app_tick(void) {
	int64_t now = now_ms();
	for (int s = 0; s < app.n_sess; s++) {
		session *ss = &app.sess[s];
		if (ss->kill) {
			session_drop(s, ss->kill_why);
			continue;
		}
		if (ss->state == SS_RETRY && now >= ss->retry_at) {
			session_connect(s);
		}
		if (ss->conn && (ss->state == SS_LOGON || ss->state == SS_ONLINE)) {
			/* any bytes count, the server's pings above all: a quiet channel is not a dead link */
			int64_t heard = ws_last_rx(ss->conn) > ss->last_rx ? ws_last_rx(ss->conn) : ss->last_rx;
			static int64_t dead_after;
			if (!dead_after) {
				const char *e = getenv("SYN_CAST_DEAD_AFTER_MS");
				dead_after = e && atoll(e) > 0 ? atoll(e) : DEAD_AFTER_MS;
			}
			if (now - heard > dead_after) {
				session_kill(s, "no answer from the server");
			} else if (now - ss->last_ping > PING_EVERY_MS) {
				ss->last_ping = now;
				if (ws_send(ss->conn, WS_PING, "syn", 3, NULL, 0) != 0) {
					session_kill(s, "connection lost");
				}
			}
		}
	}
	say_tick();
	if (app.tx.phase != TX_IDLE && now - app.tx.started > (int64_t)app.cfg.max_talk_seconds * 1000) {
		app_toast("talk time limit (%d s) reached", app.cfg.max_talk_seconds);
		if (app.tx.vox || app.tx.say_pcm) {
			tx_finish();
		} else {
			app_ptt_up();
			app.tx.started = now; /* the last packets still flush */
		}
	}
	player_tick();
	for (int i = 0; i < MAX_IMAGES_IN; i++) {
		if (app.images[i].msg_id && now - app.images[i].when > 120000) {
			hmsg *m = msg(app.images[i].channel, app.images[i].msg_id);
			if (m) {
				m->partial = false;
				history_commit(&app.chan[app.images[i].channel].hist, m);
			}
			memset(&app.images[i], 0, sizeof app.images[i]);
		}
	}
	if (app.toast[0] && now >= app.toast_until) {
		app.toast[0] = 0;
		app.dirty = true;
	}
	/* deaths noticed during this tick's sends */
	for (int s = 0; s < app.n_sess; s++) {
		if (app.sess[s].kill) {
			session_drop(s, app.sess[s].kill_why);
		}
	}
}

int app_init(void) {
	char err[400];
	int rc = config_load(&app.cfg, err, sizeof err);
	if (rc < 0) {
		LOG("%s", err);
		return -1;
	}
	app.speed = 1.0;
	if (pipe2(app.connect_pipe, O_CLOEXEC) != 0 || pipe2(app.capture_pipe, O_CLOEXEC) != 0) {
		return -1;
	}
	fcntl(app.connect_pipe[0], F_SETFL, O_NONBLOCK);
	fcntl(app.capture_pipe[0], F_SETFL, O_NONBLOCK);
	srand((unsigned)now_ms());

	mkdir_p(app.cfg.data_dir, 0700);
	for (int k = 0; k < app.cfg.n_channels; k++) {
		channel *c = &app.chan[app.n_chan];
		str_copy(c->name, sizeof c->name, app.cfg.channels[k]);
		if (history_open(&c->hist, app.cfg.data_dir, c->name) != 0) {
			LOG("cannot open history for %s in %s", c->name, app.cfg.data_dir);
			continue;
		}
		history_prune(&c->hist, app.cfg.history_days, (long long)app.cfg.history_mb << 20);
		app.n_chan++;
	}
	/* One session (Zello allows one per account) carrying the selected
	 * channel; it moves when another is selected. */
	app.n_sess = app.n_chan ? 1 : 0;
	for (int k = 0; k < app.n_chan; k++) {
		app.chan[k].session = 0;
	}
	if (app.n_chan) {
		app.sess[0].chans[app.sess[0].n_chans++] = app.active;
	}

	playback_set_gain_db(app.cfg.play_gain_db);
	if (playback_start() != 0) {
		return -1;
	}
	capture_opts o = {app.cfg.noise_suppression, app.cfg.mic_gain_db, app.cfg.vox, app.cfg.vox_threshold};
	if (capture_start(app.capture_pipe[1], &o) != 0) {
		return -1;
	}
	if (config_is_bot_account(app.cfg.username)) {
		/* Zello allows one session per account: signing in here would knock
		 * SYN-CASTER off the channel */
		for (int s = 0; s < app.n_sess; s++) {
			snprintf(app.sess[s].error, sizeof app.sess[s].error,
				"%.40s is SYN-CASTER's account; use your own (syn-cast setup)", app.cfg.username);
		}
		LOG("not signing in: %s is SYN-CASTER's account", app.cfg.username);
	} else if (config_has_account(&app.cfg)) {
		for (int s = 0; s < app.n_sess; s++) {
			session_connect(s);
		}
	}
	app.dirty = true;
	return 0;
}

void app_shutdown(void) {
	if (app.tx.phase != TX_IDLE) {
		tx_abort(NULL);
	}
	while (app.qn > 0) {
		app.playing = true;
		finish_item(false);
	}
	for (int s = 0; s < app.n_sess; s++) {
		if (app.sess[s].conn) {
			ws_flush(app.sess[s].conn);
			ws_close(app.sess[s].conn);
			app.sess[s].conn = NULL;
		}
	}
	for (int i = 0; i < MAX_RX; i++) {
		if (app.rx[i].used) {
			if (!app.rx[i].ended) {
				rx_finalize(&app.rx[i]);
			}
			rx_free(i);
		}
	}
	capture_stop();
	playback_stop();
	for (int k = 0; k < app.n_chan; k++) {
		history_close(&app.chan[k].hist);
	}
}

char *app_status_json(void) {
	json_t *o = json_object();
	json_object_set_new(o, "user", json_string(app.cfg.username));
	json_object_set_new(o, "server", json_string(app.cfg.server));
	static const char *TB[] = {"idle", "connecting", "sending", "receiving", "disabled"};
	json_object_set_new(o, "talk", json_string(TB[app_talk_state()]));
	json_object_set_new(o, "busy", json_boolean(app.busy));
	json_object_set_new(o, "offline", json_boolean(app.offline));
	json_object_set_new(o, "vox", json_boolean(capture_vox()));
	json_object_set_new(o, "speed", json_real(app.speed));
	json_object_set_new(o, "queue", json_integer(app.qn));
	int level, el;
	json_object_set_new(o, "on_air", json_string(app_rx_name(&level, &el)));
	json_t *chans = json_array();
	for (int k = 0; k < app.n_chan; k++) {
		channel *c = &app.chan[k];
		json_t *j = json_object();
		json_object_set_new(j, "name", json_string(c->name));
		json_object_set_new(j, "active", json_boolean(k == app.active));
		json_object_set_new(j, "online", json_boolean(c->online));
		json_object_set_new(j, "state", json_string(app_session_label(k)));
		json_object_set_new(j, "users_online", json_integer(c->users_online));
		json_object_set_new(j, "unread", json_integer(c->unread));
		json_object_set_new(j, "muted", json_boolean(c->muted));
		json_object_set_new(j, "messages", json_integer((json_int_t)c->hist.n));
		json_object_set_new(j, "history", json_string(c->hist.dir));
		const char *e = app.sess[c->session].error;
		if (e[0]) json_object_set_new(j, "error", json_string(e));
		json_array_append_new(chans, j);
	}
	json_object_set_new(o, "channels", chans);
	char *s = json_dumps(o, JSON_INDENT(2) | JSON_PRESERVE_ORDER);
	json_decref(o);
	return s;
}
