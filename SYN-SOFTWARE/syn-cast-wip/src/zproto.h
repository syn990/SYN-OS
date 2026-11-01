/* ------------------------------------------------------------------------
 *   The Zello Channel API, as data: command builders, event parsing, the
 *   9-byte binary packet header and the 4-byte codec header. No sockets
 *   here, so every byte of it is covered by tests/proto_test.c.
 *
 *   Wire summary (docs/REV-zello-7.19.2.md has the full reference):
 *     text frames   JSON commands with "seq"; replies echo "seq"
 *     binary frames [type:u8][id:u32 BE][sub:u32 BE][payload]
 *                   type 1 audio: id = stream_id, sub = packet_id
 *                   type 2 image: id = image/message id, sub 1 full, 2 thumb
 *     codec_header  base64 of [rate:u16 LE][frames_per_packet:u8][frame_ms:u8]
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_ZPROTO_H
#define SYN_CAST_ZPROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZP_NAME_MAX 128
#define ZP_BIN_HDR 9

enum { ZP_BIN_AUDIO = 1, ZP_BIN_IMAGE = 2 };
enum { ZP_IMAGE_FULL = 1, ZP_IMAGE_THUMB = 2 };

typedef struct {
	uint16_t rate;             /* Hz */
	uint8_t frames_per_packet; /* 1 or 2 */
	uint8_t frame_ms;          /* 20, 40, 60 */
} zp_codec;

typedef enum {
	ZEV_UNKNOWN,
	ZEV_REPLY,          /* anything carrying "seq" */
	ZEV_CHANNEL_STATUS,
	ZEV_STREAM_START,
	ZEV_STREAM_STOP,
	ZEV_TEXT,
	ZEV_IMAGE,
	ZEV_LOCATION,
	ZEV_TRANSCRIPTION,
	ZEV_ERROR,
} zev_kind;

typedef struct {
	zev_kind kind;
	char command[48];

	/* ZEV_REPLY */
	int seq;
	bool success;
	char refresh_token[1024];
	/* error text: replies, on_error, on_channel_status */
	char error[160];

	char channel[ZP_NAME_MAX];
	char from[ZP_NAME_MAX];
	char forwho[ZP_NAME_MAX]; /* "" unless private */

	/* on_channel_status */
	bool online;
	int users_online;
	bool images_ok, texting_ok, locations_ok;
	char error_type[32];

	/* streams; start_stream reply carries stream_id too */
	uint32_t stream_id;
	char codec[16];
	zp_codec codec_hdr;
	int packet_duration;

	/* send_image reply, on_image, on_text_message, on_location */
	uint32_t image_id;
	uint32_t message_id;
	int width, height;
	char source[16];

	/* text, transcription text, or a location's address; malloc'd */
	char *text;
	double latitude, longitude, accuracy;
	double confidence;
} zevent;

/* Parses one JSON text frame. Returns 0, or -1 for frames that are not a
 * JSON object. Unknown commands come back as ZEV_UNKNOWN. */
int zp_parse(const char *json, size_t n, zevent *ev);
void zp_event_free(zevent *ev);

/* Command builders. Each returns a malloc'd compact JSON string. forwho
 * may be NULL or "" for the whole channel. */
char *zp_logon(int seq, const char *auth_token, const char *refresh_token,
	const char *username, const char *password, const char *const *channels,
	size_t n_channels, bool listen_only, bool transcriptions);
char *zp_start_stream(int seq, const char *channel, const char *forwho, const zp_codec *c);
char *zp_stop_stream(int seq, const char *channel, uint32_t stream_id);
char *zp_send_text(int seq, const char *channel, const char *forwho, const char *text);
char *zp_send_image(int seq, const char *channel, const char *forwho, int width, int height,
	size_t thumb_len, size_t full_len);
char *zp_send_location(int seq, const char *channel, const char *forwho, double lat,
	double lon, double accuracy, const char *address);

/* codec_header <-> struct. encode writes NUL-terminated base64 (9 bytes). */
void zp_codec_encode(const zp_codec *c, char out[16]);
int zp_codec_decode(const char *b64, zp_codec *c);

/* Binary header writer and parser. */
void zp_bin_header(uint8_t out[ZP_BIN_HDR], uint8_t type, uint32_t id, uint32_t sub);
typedef struct {
	uint8_t type;
	uint32_t id;
	uint32_t sub;
	const uint8_t *data;
	size_t len;
} zp_bin;
int zp_parse_bin(const uint8_t *b, size_t n, zp_bin *out);

#endif
