/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "zproto.h"
#include "util.h"

#include <jansson.h>
#include <stdlib.h>
#include <string.h>

static void get_str(json_t *o, const char *key, char *dst, size_t cap) {
	json_t *v = json_object_get(o, key);
	str_copy(dst, cap, json_is_string(v) ? json_string_value(v) : "");
}

static double get_num(json_t *o, const char *key) {
	json_t *v = json_object_get(o, key);
	return json_is_number(v) ? json_number_value(v) : 0.0;
}

static bool get_bool(json_t *o, const char *key) {
	return json_is_true(json_object_get(o, key));
}

static char *dup_str(json_t *o, const char *key) {
	json_t *v = json_object_get(o, key);
	return json_is_string(v) ? strdup(json_string_value(v)) : NULL;
}

static const struct {
	const char *name;
	zev_kind kind;
} EVENTS[] = {
	{"on_channel_status", ZEV_CHANNEL_STATUS},
	{"on_stream_start", ZEV_STREAM_START},
	{"on_stream_stop", ZEV_STREAM_STOP},
	{"on_text_message", ZEV_TEXT},
	{"on_image", ZEV_IMAGE},
	{"on_location", ZEV_LOCATION},
	{"on_transcription", ZEV_TRANSCRIPTION},
	{"on_error", ZEV_ERROR},
};

int zp_parse(const char *json, size_t n, zevent *ev) {
	memset(ev, 0, sizeof *ev);
	json_error_t jerr;
	json_t *o = json_loadb(json, n, 0, &jerr);
	if (!json_is_object(o)) {
		json_decref(o);
		return -1;
	}
	get_str(o, "command", ev->command, sizeof ev->command);
	get_str(o, "error", ev->error, sizeof ev->error);
	get_str(o, "channel", ev->channel, sizeof ev->channel);
	get_str(o, "from", ev->from, sizeof ev->from);
	/* "for" is a username on a private message, false otherwise */
	get_str(o, "for", ev->forwho, sizeof ev->forwho);
	ev->stream_id = (uint32_t)get_num(o, "stream_id");
	ev->message_id = (uint32_t)get_num(o, "message_id");

	if (ev->command[0]) {
		ev->kind = ZEV_UNKNOWN;
		for (size_t i = 0; i < ARRAY_LEN(EVENTS); i++) {
			if (strcmp(ev->command, EVENTS[i].name) == 0) {
				ev->kind = EVENTS[i].kind;
			}
		}
	} else if (json_object_get(o, "seq")) {
		ev->kind = ZEV_REPLY;
	}

	switch (ev->kind) {
	case ZEV_REPLY:
		ev->seq = (int)get_num(o, "seq");
		ev->success = get_bool(o, "success");
		ev->image_id = (uint32_t)get_num(o, "image_id");
		get_str(o, "refresh_token", ev->refresh_token, sizeof ev->refresh_token);
		break;
	case ZEV_CHANNEL_STATUS: {
		char status[16];
		get_str(o, "status", status, sizeof status);
		ev->online = strcmp(status, "online") == 0;
		ev->users_online = (int)get_num(o, "users_online");
		ev->images_ok = get_bool(o, "images_supported");
		ev->texting_ok = get_bool(o, "texting_supported");
		ev->locations_ok = get_bool(o, "locations_supported");
		get_str(o, "error_type", ev->error_type, sizeof ev->error_type);
		break;
	}
	case ZEV_STREAM_START: {
		get_str(o, "codec", ev->codec, sizeof ev->codec);
		char hdr[32];
		get_str(o, "codec_header", hdr, sizeof hdr);
		if (zp_codec_decode(hdr, &ev->codec_hdr) != 0) {
			ev->codec_hdr = (zp_codec){16000, 1, 60};
		}
		ev->packet_duration = (int)get_num(o, "packet_duration");
		break;
	}
	case ZEV_TEXT:
		ev->text = dup_str(o, "text");
		break;
	case ZEV_IMAGE:
		ev->width = (int)get_num(o, "width");
		ev->height = (int)get_num(o, "height");
		get_str(o, "source", ev->source, sizeof ev->source);
		break;
	case ZEV_LOCATION:
		ev->latitude = get_num(o, "latitude");
		ev->longitude = get_num(o, "longitude");
		ev->accuracy = get_num(o, "accuracy");
		/* the reference table says formatted_address, its example says rgl */
		ev->text = dup_str(o, "formatted_address");
		if (!ev->text) {
			ev->text = dup_str(o, "rgl");
		}
		break;
	case ZEV_TRANSCRIPTION:
		ev->text = dup_str(o, "text");
		ev->confidence = get_num(o, "confidence");
		get_str(o, "sender", ev->from, sizeof ev->from);
		break;
	default:
		break;
	}
	json_decref(o);
	return 0;
}

void zp_event_free(zevent *ev) {
	free(ev->text);
	ev->text = NULL;
}

static char *finish(json_t *o) {
	char *s = json_dumps(o, JSON_COMPACT);
	json_decref(o);
	return s;
}

static json_t *command(const char *name, int seq, const char *channel, const char *forwho) {
	json_t *o = json_object();
	json_object_set_new(o, "command", json_string(name));
	json_object_set_new(o, "seq", json_integer(seq));
	if (channel) {
		json_object_set_new(o, "channel", json_string(channel));
	}
	if (forwho && *forwho) {
		json_object_set_new(o, "for", json_string(forwho));
	}
	return o;
}

char *zp_logon(int seq, const char *auth_token, const char *refresh_token,
	const char *username, const char *password, const char *const *channels,
	size_t n_channels, bool listen_only, bool transcriptions) {
	json_t *o = command("logon", seq, NULL, NULL);
	if (refresh_token && *refresh_token) {
		json_object_set_new(o, "refresh_token", json_string(refresh_token));
	} else if (auth_token && *auth_token) {
		json_object_set_new(o, "auth_token", json_string(auth_token));
	}
	if (username && *username) {
		json_object_set_new(o, "username", json_string(username));
		json_object_set_new(o, "password", json_string(password ? password : ""));
	}
	json_t *arr = json_array();
	for (size_t i = 0; i < n_channels; i++) {
		json_array_append_new(arr, json_string(channels[i]));
	}
	json_object_set_new(o, "channels", arr);
	if (listen_only) {
		json_object_set_new(o, "listen_only", json_true());
	}
	if (transcriptions) {
		json_t *f = json_object();
		json_object_set_new(f, "transcriptions", json_true());
		json_object_set_new(o, "features", f);
	}
	json_object_set_new(o, "platform_type", json_string("SYN-OS"));
	json_object_set_new(o, "platform_name", json_string("syn-cast"));
	return finish(o);
}

char *zp_start_stream(int seq, const char *channel, const char *forwho, const zp_codec *c) {
	json_t *o = command("start_stream", seq, channel, forwho);
	char hdr[16];
	zp_codec_encode(c, hdr);
	json_object_set_new(o, "type", json_string("audio"));
	json_object_set_new(o, "codec", json_string("opus"));
	json_object_set_new(o, "codec_header", json_string(hdr));
	json_object_set_new(o, "packet_duration", json_integer(c->frame_ms * c->frames_per_packet));
	return finish(o);
}

char *zp_stop_stream(int seq, const char *channel, uint32_t stream_id) {
	json_t *o = command("stop_stream", seq, channel, NULL);
	json_object_set_new(o, "stream_id", json_integer(stream_id));
	return finish(o);
}

char *zp_send_text(int seq, const char *channel, const char *forwho, const char *text) {
	json_t *o = command("send_text_message", seq, channel, forwho);
	json_object_set_new(o, "text", json_string(text));
	return finish(o);
}

char *zp_send_image(int seq, const char *channel, const char *forwho, int width, int height,
	size_t thumb_len, size_t full_len) {
	json_t *o = command("send_image", seq, channel, forwho);
	json_object_set_new(o, "type", json_string("jpeg"));
	json_object_set_new(o, "thumbnail_content_length", json_integer((json_int_t)thumb_len));
	json_object_set_new(o, "content_length", json_integer((json_int_t)full_len));
	json_object_set_new(o, "width", json_integer(width));
	json_object_set_new(o, "height", json_integer(height));
	json_object_set_new(o, "source", json_string("library"));
	return finish(o);
}

char *zp_send_location(int seq, const char *channel, const char *forwho, double lat,
	double lon, double accuracy, const char *address) {
	json_t *o = command("send_location", seq, channel, forwho);
	json_object_set_new(o, "latitude", json_real(lat));
	json_object_set_new(o, "longitude", json_real(lon));
	json_object_set_new(o, "accuracy", json_real(accuracy));
	json_object_set_new(o, "formatted_address", json_string(address ? address : ""));
	return finish(o);
}

void zp_codec_encode(const zp_codec *c, char out[16]) {
	uint8_t b[4] = {(uint8_t)(c->rate & 0xFF), (uint8_t)(c->rate >> 8), c->frames_per_packet, c->frame_ms};
	char *s = b64_encode(b, 4, false);
	str_copy(out, 16, s ? s : "");
	free(s);
}

int zp_codec_decode(const char *b64, zp_codec *c) {
	uint8_t b[8];
	if (b64_decode(b64, b, sizeof b) != 4) {
		return -1;
	}
	c->rate = (uint16_t)(b[0] | (b[1] << 8));
	c->frames_per_packet = b[2];
	c->frame_ms = b[3];
	if (c->rate == 0 || c->frame_ms == 0 || c->frames_per_packet == 0) {
		return -1;
	}
	return 0;
}

static void put_be32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void zp_bin_header(uint8_t out[ZP_BIN_HDR], uint8_t type, uint32_t id, uint32_t sub) {
	out[0] = type;
	put_be32(out + 1, id);
	put_be32(out + 5, sub);
}

int zp_parse_bin(const uint8_t *b, size_t n, zp_bin *out) {
	if (n < ZP_BIN_HDR) {
		return -1;
	}
	out->type = b[0];
	out->id = get_be32(b + 1);
	out->sub = get_be32(b + 5);
	out->data = b + ZP_BIN_HDR;
	out->len = n - ZP_BIN_HDR;
	return 0;
}
