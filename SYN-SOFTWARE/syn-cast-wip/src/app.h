/* ------------------------------------------------------------------------
 *   The core: sessions, channels, the talk button, incoming streams, the
 *   playback queue and history, all driven from one poll loop. The window
 *   (ui.c) and the control socket (ctl.c) only call the app_* actions and
 *   read this state; neither touches the network or audio directly.
 *
 *   One session carries every channel: SYN-CAST protocol v1's hello joins
 *   them all at once, and the selected channel is where what you send
 *   goes. An OFFLINE switch closes the session and keeps the token.
 *
 *   Everything sent goes to the whole selected channel. A direct message
 *   that arrives for this member is shown in the selected channel, marked
 *   private.
 *
 *   The talk button has the five states the phone app's TalkButtonState
 *   has: IDLE, CONNECTING (pressed, waiting for the server to open the
 *   stream), SENDING, RECEIVING, DISABLED (channel offline).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_APP_H
#define SYN_CAST_APP_H

#include "audio.h"
#include "config.h"
#include "history.h"
#include "stretch.h"
#include "ws.h"
#include "zproto.h"

#include <stdbool.h>
#include <stdint.h>

#define SYN_CAST_VERSION "0.2.0"
#define MAX_SESSIONS CFG_MAX_CHANNELS
#define MAX_RX 16
#define MAX_QUEUE 32
#define MAX_PENDING 32
#define MAX_IMAGES_IN 8

typedef enum { SS_OFF, SS_CONNECTING, SS_LOGON, SS_ONLINE, SS_RETRY } sess_state;
typedef enum { TB_IDLE, TB_CONNECTING, TB_SENDING, TB_RECEIVING, TB_DISABLED } talk_state;
typedef enum { P_NONE, P_HELLO, P_TALK, P_OVER, P_SAY, P_PIC, P_LOC, P_PRESENCE, P_ROOM } pend_kind;

/* A growable list of opus packets; length 0 marks a lost one. */
typedef struct {
	uint8_t *buf;
	size_t len, cap;
	uint32_t *off;
	uint16_t *plen;
	size_t n, ncap;
} pkt_list;

typedef struct {
	int seq;
	pend_kind kind;
	int channel;
	uint64_t ref;
	int64_t when;
} pending;

typedef struct {
	sess_state state;
	ws *conn;
	uint64_t cookie;
	int seq;
	int64_t retry_at;
	int backoff_ms;
	int64_t last_rx, last_ping;
	char error[160];
	/* set inside frame callbacks; the connection is dropped once the
	 * callback has returned, never under ws_read() */
	bool kill;
	char kill_why[160];
	pending pend[MAX_PENDING];
	int chans[CFG_MAX_CHANNELS];   /* channel indexes this session carries */
	int n_chans;
	bool was_online;
	bool refused;   /* sign-in refused: no retries until the account is fixed */
	/* another session of this member and device replaced this one (close
	 * 4003). The first time, this one takes it straight back; replaced again
	 * within KICK_WINDOW_MS, something is fighting, so it goes offline */
	bool kicked;
	int64_t last_kick;
} session;

typedef struct {
	char name[128];
	int session;
	bool online;
	char who[ZP_ONLINE_MAX][64]; /* members online in it, by name (join and leave keep it) */
	int users_online;            /* their count (may exceed ZP_ONLINE_MAX) */
	char error[160];
	history hist;
	int unread;
	bool muted;              /* G278: switched off (saved on the server): no sound, no alert, notes kept */
} channel;

typedef struct {
	bool used;
	int session, channel;
	uint32_t stream_id;
	char from[64], forwho[64]; /* forwho: this member, on a direct message */
	zp_codec codec;
	uint64_t msg_id;
	hv_writer file;
	uint32_t last_pid;
	bool have_pid;
	pkt_list pkts;
	bool ended;
	bool queued;
	int64_t started;
} rx_stream;

typedef enum { PI_LIVE, PI_REPLAY } pi_kind;
typedef struct {
	pi_kind kind;
	int rx;              /* PI_LIVE */
	int channel;
	uint64_t msg_id;
	char from[64];
	hv_file file;        /* PI_REPLAY */
	size_t cursor;
	rx_decoder dec;
	stretch *st;
	bool started;
	bool fed_all;
	uint64_t end_mark;
	int64_t began;
	int duration_ms;     /* known for replays */
} play_item;

typedef enum { TX_IDLE, TX_STARTING, TX_SENDING } tx_phase;
typedef struct {
	tx_phase phase;
	bool released;       /* the button is up; waiting for the last packets */
	bool end_seen;       /* capture (or the file) has finished */
	bool vox;            /* opened by voice, not by the button */
	bool server_over;    /* the server ended the floor (stall, long, flood): no over to send */
	int channel;
	int start_seq;
	uint32_t stream_id;
	pkt_list held;       /* captured before the server opened the stream */
	uint64_t msg_id;
	hv_writer file;
	int64_t started;
	int level;
	/* talking a WAV file in (ctl SAY) instead of the mic */
	int16_t *say_pcm;
	size_t say_n, say_pos;
	int64_t say_t0;
	int say_sent;
	OpusEncoder *say_enc;
} tx_state;

typedef struct {
	uint32_t blob;       /* the binary frame's id */
	int channel;
	uint64_t msg_id;
	int64_t when;
} image_in;

typedef struct {
	config cfg;
	char member[128];         /* who the server says we are (the hello answer) */
	session sess[MAX_SESSIONS];
	int n_sess;
	channel chan[CFG_MAX_CHANNELS];
	int n_chan;
	int active;               /* the selected channel; everything sent goes to all of it */

	rx_stream rx[MAX_RX];
	play_item queue[MAX_QUEUE];
	int qn;
	bool playing;             /* queue[0] is on air */
	double speed;             /* replay speed: 1, 1.25, 1.5 */
	bool offline;             /* switched off: no session until switched back on */

	tx_state tx;
	image_in images[MAX_IMAGES_IN];

	struct {
		uint8_t *thumb, *full;
		size_t thumb_n, full_n;
		int w, h;
		int channel;
		int seq;
		bool busy;
	} img_out;

	char snapshot[1024];      /* ctl SNAPSHOT: the next frame goes to this PPM */
	char toast[200];
	int64_t toast_until;
	bool dirty;
	bool focused;             /* window has keyboard focus */
	bool quit;

	int connect_pipe[2];
	int capture_pipe[2];
	uint64_t next_cookie;
} app_t;

extern app_t app;

int app_init(void);
void app_shutdown(void);

/* poll plumbing */
int app_timeout_ms(void);
void app_tick(void);
void app_on_connect_pipe(void);
void app_on_capture_pipe(void);
/* fills fds/events for sessions; returns count */
int app_session_fds(int *fds, short *events, int *sess_idx, int max);
void app_on_session_io(int s, short revents);

/* actions */
void app_ptt_down(void);
void app_ptt_up(void);
void app_ptt_toggle(void);
int app_say_file(const char *wav_path, char *err, size_t errn);
int app_send_text(const char *text, char *err, size_t errn);
int app_send_image(const char *jpeg_path, char *err, size_t errn);
int app_send_location(double lat, double lon, const char *address, char *err, size_t errn);
void app_select_channel(int idx);
int app_find_channel(const char *name);
void app_replay(int channel, uint64_t msg_id);
void app_replay_last(void);
void app_stop_playback(void);
void app_cycle_speed(void);
void app_toggle_room(void);
int app_play_missed(int channel);
void app_toggle_vox(void);
void app_set_offline(bool off);
void app_toast(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* A line typed into the window: a text message, or a /command. */
void app_input(const char *line);
extern const char *const APP_HELP;

talk_state app_talk_state(void);
const char *app_session_label(int channel);
/* who is on air right now ("" if nobody), and the level 0..100 */
const char *app_rx_name(int *level, int *elapsed_ms);

/* status as JSON for `syn-cast status` */
char *app_status_json(void);

#endif
