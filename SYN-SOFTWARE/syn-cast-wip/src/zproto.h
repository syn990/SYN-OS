/* ------------------------------------------------------------------------
 *   SYN-CAST protocol v1 as data (SYN-CASTER's docs/cast-protocol.md):
 *   request builders, the parse of replies and events, and the 9-byte
 *   binary frame header. No sockets here, so every byte of it is covered
 *   by tests/proto_test.c.
 *
 *   Wire summary:
 *     text frames   one JSON object: a request carries "type" and "seq",
 *                   its answer is {"type":"ok"|"error","seq":N,...};
 *                   events carry "type" and no "seq"
 *     binary frames [kind:u8][id:u32 BE][seq:u32 BE][payload]
 *                   kind 1 audio: id = stream, seq = packet number from 0
 *                   kind 2 picture: id = upload (sent) or blob (received)
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_ZPROTO_H
#define SYN_CAST_ZPROTO_H

#include <jansson.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZP_VERSION 1
#define ZP_PATH "/cast/v1"
#define ZP_NAME_MAX 128
#define ZP_ID_MAX 64          /* a message id */
#define ZP_TOKEN_MAX 2048
#define ZP_MAX_CHANNELS 16
#define ZP_ONLINE_MAX 32      /* members listed online per channel */
#define ZP_BIN_HDR 9
#define ZP_TEXT_MAX 4000
#define ZP_PACKET_MAX 4000

enum { ZP_BIN_AUDIO = 1, ZP_BIN_PIC = 2 };

/* How a voice stream is cut: Opus at rate Hz, frames_per_packet frames of
 * frame_ms each in one packet. The wire's frame_ms is their product; the
 * voice files (history.c) keep the three fields. */
typedef struct {
	uint16_t rate;
	uint8_t frames_per_packet;
	uint8_t frame_ms;
} zp_codec;

typedef enum {
	ZEV_UNKNOWN,
	ZEV_REPLY,      /* ok or error carrying "seq" */
	ZEV_TALK,       /* someone took the floor (or a replay for this session) */
	ZEV_OVER,       /* the floor is free again */
	ZEV_SAY,
	ZEV_PIC,        /* followed by one binary frame, kind 2, id = blob */
	ZEV_LOC,
	ZEV_PRESENCE,
	ZEV_JOIN,
	ZEV_LEAVE,
	ZEV_ERROR,      /* an error with no seq: revoked, not_logged_in, a bad packet */
} zev_kind;

/* One channel of a hello reply (or the join reply). */
typedef struct {
	char id[ZP_NAME_MAX];
	char name[ZP_NAME_MAX];
	char kind[16];
	int online;              /* members with a session in it... */
	char who[ZP_ONLINE_MAX][64]; /* ...and the first ZP_ONLINE_MAX of them */
	char talking[ZP_NAME_MAX];
} zp_chan;

typedef struct {
	zev_kind kind;
	char type[24];

	/* ZEV_REPLY and ZEV_ERROR */
	int seq;
	bool ok;
	char code[32];           /* error code; "" on ok */
	char error[200];         /* error text */
	char service[32];        /* forbidden: the missing service */
	char talker[ZP_NAME_MAX];/* busy: who holds the floor */

	/* hello reply */
	char member[ZP_NAME_MAX];
	char session[ZP_NAME_MAX];
	char token[ZP_TOKEN_MAX];/* only after an invite: keep it */
	char token_id[64];
	zp_chan channels[ZP_MAX_CHANNELS];
	int n_channels;
	struct { char channel[ZP_NAME_MAX]; char code[32]; } refused[ZP_MAX_CHANNELS];
	int n_refused;
	int held;                /* direct messages waiting, all peers together */

	/* join reply: channels[0]; talk reply and events: stream; over reply
	 * and event: message, ms, packets; say, loc replies: message; pic
	 * reply: upload */
	uint32_t stream;
	uint32_t upload;
	char message[ZP_ID_MAX];
	int ms, packets;

	/* events: where it came from and where it goes */
	char channel[ZP_NAME_MAX];
	char from[ZP_NAME_MAX];
	char to[ZP_NAME_MAX];    /* a direct message: the receiver (this member) */
	char at[32];
	/* G228: who spoke, when a cast member speaks for a character (talk,
	 * over, say, loc, pic; history lines carry the same): the character's
	 * id, display name and #rrggbb colour; "" when the member spoke as
	 * themselves. A portrait reply names its member, name and colour too. */
	char as[ZP_NAME_MAX];
	char name[64];
	char colour[16];
	char *png;               /* portrait reply: the PNG, base64; malloc'd */

	/* talk */
	zp_codec codec;
	bool replay;
	/* over */
	char reason[16];
	/* say: text; loc: address; presence: text; malloc'd */
	char *text;
	double lat, lon, accuracy;
	/* pic */
	char mime[32];
	int width, height;
	size_t size;
	uint32_t blob;
	/* presence */
	char status[16];
	int devices;
	/* history reply: the record lines, oldest first (owned, freed by
	 * zp_event_free), and whether older ones exist */
	json_t *messages;
	bool more;
} zevent;

/* Parses one JSON text frame. Returns 0, or -1 for frames that are not a
 * JSON object. An unknown type comes back as ZEV_UNKNOWN. */
int zp_parse(const char *json, size_t n, zevent *ev);
void zp_event_free(zevent *ev);

/* Request builders. Each returns a malloc'd compact JSON string. Exactly
 * one of channel and to names the conversation (to may be NULL or ""). */
char *zp_hello(int seq, const char *token, const char *invite, const char *device, const char *client,
	const char *const *channels, size_t n_channels, bool all_channels);
char *zp_join(int seq, const char *channel);
char *zp_leave(int seq, const char *channel, bool drop);
char *zp_talk(int seq, const char *channel, const char *to, const zp_codec *c);
char *zp_over(int seq, uint32_t stream);
char *zp_say(int seq, const char *channel, const char *to, const char *text);
char *zp_pic(int seq, const char *channel, const char *to, const char *mime, int width, int height, size_t size);
char *zp_loc(int seq, const char *channel, const char *to, double lat, double lon, double accuracy,
	const char *address);
char *zp_presence(int seq, const char *status, const char *text);
char *zp_history(int seq, const char *channel, const char *with, int limit, const char *before);
/* G228: asks for a member's or character's name, colour and portrait. */
char *zp_portrait(int seq, const char *member);
char *zp_history_play(int seq, const char *channel, const char *with, const char *message);

/* Binary header writer and parser. */
void zp_bin_header(uint8_t out[ZP_BIN_HDR], uint8_t kind, uint32_t id, uint32_t seq);
typedef struct {
	uint8_t kind;
	uint32_t id;
	uint32_t seq;
	const uint8_t *data;
	size_t len;
} zp_bin;
int zp_parse_bin(const uint8_t *b, size_t n, zp_bin *out);

#endif
