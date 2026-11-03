/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "zproto.h"
#include "util.h"

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

static void get_chan(json_t *o, zp_chan *c) {
	memset(c, 0, sizeof *c);
	get_str(o, "channel", c->id, sizeof c->id);
	get_str(o, "name", c->name, sizeof c->name);
	if (!c->name[0]) {
		str_copy(c->name, sizeof c->name, c->id);
	}
	get_str(o, "kind", c->kind, sizeof c->kind);
	json_t *online = json_object_get(o, "online");
	if (json_is_array(online)) {
		size_t k;
		json_t *v;
		json_array_foreach(online, k, v) {
			if (json_is_string(v)) {
				if (c->online < ZP_ONLINE_MAX) {
					str_copy(c->who[c->online], sizeof c->who[0], json_string_value(v));
				}
				c->online++;
			}
		}
	}
	get_str(o, "talking", c->talking, sizeof c->talking);
}

static const struct {
	const char *name;
	zev_kind kind;
} EVENTS[] = {
	{"talk", ZEV_TALK},       {"over", ZEV_OVER},   {"say", ZEV_SAY},   {"pic", ZEV_PIC},
	{"loc", ZEV_LOC},         {"presence", ZEV_PRESENCE}, {"join", ZEV_JOIN}, {"leave", ZEV_LEAVE},
	{"rooms", ZEV_ROOMS},
};

int zp_parse(const char *json, size_t n, zevent *ev) {
	memset(ev, 0, sizeof *ev);
	json_error_t jerr;
	json_t *o = json_loadb(json, n, 0, &jerr);
	if (!json_is_object(o)) {
		json_decref(o);
		return -1;
	}
	get_str(o, "type", ev->type, sizeof ev->type);
	get_str(o, "channel", ev->channel, sizeof ev->channel);
	get_str(o, "from", ev->from, sizeof ev->from);
	get_str(o, "to", ev->to, sizeof ev->to);
	get_str(o, "at", ev->at, sizeof ev->at);
	get_str(o, "message", ev->message, sizeof ev->message);
	ev->stream = (uint32_t)get_num(o, "stream");
	ev->ms = (int)get_num(o, "ms");

	if (strcmp(ev->type, "ok") == 0 || strcmp(ev->type, "error") == 0) {
		/* an answer echoes the request's seq; an error without one is the server's own */
		ev->kind = json_is_integer(json_object_get(o, "seq")) ? ZEV_REPLY : ZEV_ERROR;
	} else {
		ev->kind = ZEV_UNKNOWN;
		for (size_t i = 0; i < ARRAY_LEN(EVENTS); i++) {
			if (strcmp(ev->type, EVENTS[i].name) == 0) {
				ev->kind = EVENTS[i].kind;
			}
		}
	}

	switch (ev->kind) {
	case ZEV_REPLY:
	case ZEV_ERROR: {
		ev->seq = (int)get_num(o, "seq");
		ev->ok = strcmp(ev->type, "ok") == 0;
		get_str(o, "code", ev->code, sizeof ev->code);
		get_str(o, "text", ev->error, sizeof ev->error);
		get_str(o, "service", ev->service, sizeof ev->service);
		get_str(o, "talker", ev->talker, sizeof ev->talker);
		if (!ev->ok) {
			if (!ev->error[0]) {
				str_copy(ev->error, sizeof ev->error, ev->code[0] ? ev->code : "refused");
			}
			break;
		}
		/* hello */
		get_str(o, "member", ev->member, sizeof ev->member);
		get_str(o, "session", ev->session, sizeof ev->session);
		get_str(o, "token", ev->token, sizeof ev->token);
		get_str(o, "token_id", ev->token_id, sizeof ev->token_id);
		json_t *chans = json_object_get(o, "channels");
		if (json_is_array(chans)) {
			size_t k;
			json_t *v;
			json_array_foreach(chans, k, v) {
				if (json_is_object(v) && ev->n_channels < ZP_MAX_CHANNELS) {
					get_chan(v, &ev->channels[ev->n_channels++]);
				}
			}
		} else if (json_is_string(json_object_get(o, "channel")) && json_object_get(o, "members")) {
			/* a join reply is one channel's record */
			get_chan(o, &ev->channels[0]);
			ev->n_channels = 1;
		}
		json_t *refused = json_object_get(o, "refused");
		if (json_is_array(refused)) {
			size_t k;
			json_t *v;
			json_array_foreach(refused, k, v) {
				if (json_is_object(v) && ev->n_refused < ZP_MAX_CHANNELS) {
					get_str(v, "channel", ev->refused[ev->n_refused].channel, ZP_NAME_MAX);
					get_str(v, "code", ev->refused[ev->n_refused].code, 32);
					ev->n_refused++;
				}
			}
		}
		json_t *off = json_object_get(o, "off");
		if (json_is_array(off)) {
			size_t k;
			json_t *v;
			json_array_foreach(off, k, v) {
				if (json_is_string(v) && ev->n_off < ZP_MAX_CHANNELS) {
					str_copy(ev->off[ev->n_off++], ZP_NAME_MAX, json_string_value(v));
				}
			}
		}
		json_t *held = json_object_get(o, "held");
		if (json_is_array(held)) {
			size_t k;
			json_t *v;
			json_array_foreach(held, k, v) {
				ev->held += (int)get_num(v, "count");
			}
		}
		/* talk, over, say, loc, pic, history */
		ev->upload = (uint32_t)get_num(o, "upload");
		ev->packets = (int)get_num(o, "packets");
		json_t *msgs = json_object_get(o, "messages");
		if (json_is_array(msgs)) {
			ev->messages = json_incref(msgs);
			ev->more = get_bool(o, "more");
		}
		break;
	}
	case ZEV_TALK: {
		int fm = (int)get_num(o, "frame_ms");
		int rate = (int)get_num(o, "rate");
		ev->codec.rate = (uint16_t)(rate > 0 && rate < 65536 ? rate : 16000);
		ev->codec.frames_per_packet = 1;
		ev->codec.frame_ms = (uint8_t)(fm == 20 || fm == 40 || fm == 60 ? fm : 20);
		ev->replay = get_bool(o, "replay");
		break;
	}
	case ZEV_OVER:
		get_str(o, "reason", ev->reason, sizeof ev->reason);
		ev->replay = get_bool(o, "replay");
		break;
	case ZEV_SAY:
		ev->text = dup_str(o, "text");
		break;
	case ZEV_PIC:
		get_str(o, "mime", ev->mime, sizeof ev->mime);
		ev->width = (int)get_num(o, "width");
		ev->height = (int)get_num(o, "height");
		ev->size = (size_t)get_num(o, "size");
		ev->blob = (uint32_t)get_num(o, "blob");
		break;
	case ZEV_LOC:
		ev->lat = get_num(o, "lat");
		ev->lon = get_num(o, "lon");
		ev->accuracy = get_num(o, "accuracy");
		ev->text = dup_str(o, "address");
		break;
	case ZEV_ROOMS: {
		/* G278: each room's saved switch, so a 'channel off' by the crew or another device reaches this one live */
		json_t *rooms = json_object_get(o, "rooms");
		size_t k;
		json_t *v;
		if (json_is_array(rooms)) {
			json_array_foreach(rooms, k, v) {
				const char *id = json_string_value(json_object_get(v, "channel"));
				json_t *off = json_object_get(v, "off");
				if (!id || !json_is_boolean(off)) {
					continue;
				}
				if (json_is_true(off) && ev->n_off < ZP_MAX_CHANNELS) {
					str_copy(ev->off[ev->n_off++], ZP_NAME_MAX, id);
				} else if (!json_is_true(off) && ev->n_on < ZP_MAX_CHANNELS) {
					str_copy(ev->on[ev->n_on++], ZP_NAME_MAX, id);
				}
			}
		}
		break;
	}
	case ZEV_PRESENCE:
		get_str(o, "member", ev->member, sizeof ev->member);
		get_str(o, "status", ev->status, sizeof ev->status);
		ev->text = dup_str(o, "text");
		ev->devices = (int)get_num(o, "devices");
		break;
	case ZEV_JOIN:
	case ZEV_LEAVE:
		get_str(o, "member", ev->member, sizeof ev->member);
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
	if (ev->messages) {
		json_decref(ev->messages);
		ev->messages = NULL;
	}
}

/* ---------- builders ---------- */

static char *finish(json_t *o) {
	char *s = json_dumps(o, JSON_COMPACT);
	json_decref(o);
	return s;
}

static json_t *request(const char *type, int seq) {
	json_t *o = json_object();
	json_object_set_new(o, "type", json_string(type));
	json_object_set_new(o, "seq", json_integer(seq));
	return o;
}

static void address(json_t *o, const char *channel, const char *to, const char *dm_field) {
	if (to && *to) {
		json_object_set_new(o, dm_field, json_string(to));
	} else if (channel && *channel) {
		json_object_set_new(o, "channel", json_string(channel));
	}
}

char *zp_hello(int seq, const char *token, const char *invite, const char *device, const char *client,
	const char *const *channels, size_t n_channels, bool all_channels) {
	json_t *o = request("hello", seq);
	json_object_set_new(o, "v", json_integer(ZP_VERSION));
	if (invite && *invite) {
		json_object_set_new(o, "invite", json_string(invite));
	} else {
		json_object_set_new(o, "token", json_string(token ? token : ""));
	}
	json_object_set_new(o, "device", json_string(device && *device ? device : "window"));
	json_object_set_new(o, "client", json_string(client ? client : "syn-cast"));
	if (!all_channels) {
		json_t *arr = json_array();
		for (size_t i = 0; i < n_channels; i++) {
			json_array_append_new(arr, json_string(channels[i]));
		}
		json_object_set_new(o, "channels", arr);
	}
	return finish(o);
}

char *zp_join(int seq, const char *channel) {
	json_t *o = request("join", seq);
	json_object_set_new(o, "channel", json_string(channel));
	return finish(o);
}

char *zp_leave(int seq, const char *channel, bool drop) {
	json_t *o = request("leave", seq);
	json_object_set_new(o, "channel", json_string(channel));
	if (drop) {
		json_object_set_new(o, "drop", json_true());
	}
	return finish(o);
}

char *zp_talk(int seq, const char *channel, const char *to, const zp_codec *c) {
	json_t *o = request("talk", seq);
	address(o, channel, to, "to");
	json_object_set_new(o, "codec", json_string("opus"));
	json_object_set_new(o, "rate", json_integer(c->rate));
	json_object_set_new(o, "channels", json_integer(1));
	json_object_set_new(o, "frame_ms", json_integer(c->frame_ms * (c->frames_per_packet ? c->frames_per_packet : 1)));
	return finish(o);
}

char *zp_over(int seq, uint32_t stream) {
	json_t *o = request("over", seq);
	json_object_set_new(o, "stream", json_integer(stream));
	return finish(o);
}

char *zp_say(int seq, const char *channel, const char *to, const char *text) {
	json_t *o = request("say", seq);
	address(o, channel, to, "to");
	json_object_set_new(o, "text", json_string(text));
	return finish(o);
}

char *zp_pic(int seq, const char *channel, const char *to, const char *mime, int width, int height, size_t size) {
	json_t *o = request("pic", seq);
	address(o, channel, to, "to");
	json_object_set_new(o, "mime", json_string(mime));
	json_object_set_new(o, "width", json_integer(width));
	json_object_set_new(o, "height", json_integer(height));
	json_object_set_new(o, "size", json_integer((json_int_t)size));
	return finish(o);
}

char *zp_loc(int seq, const char *channel, const char *to, double lat, double lon, double accuracy,
	const char *address_text) {
	json_t *o = request("loc", seq);
	address(o, channel, to, "to");
	json_object_set_new(o, "lat", json_real(lat));
	json_object_set_new(o, "lon", json_real(lon));
	json_object_set_new(o, "accuracy", json_real(accuracy));
	if (address_text && *address_text) {
		json_object_set_new(o, "address", json_string(address_text));
	}
	return finish(o);
}

char *zp_presence(int seq, const char *status, const char *text) {
	json_t *o = request("presence", seq);
	json_object_set_new(o, "status", json_string(status));
	if (text && *text) {
		json_object_set_new(o, "text", json_string(text));
	}
	return finish(o);
}

char *zp_room(int seq, const char *channel, bool on) {
	json_t *o = request("room", seq);
	json_object_set_new(o, "channel", json_string(channel));
	json_object_set_new(o, "on", json_boolean(on));
	return finish(o);
}

char *zp_history(int seq, const char *channel, const char *with, int limit, const char *before) {
	json_t *o = request("history", seq);
	address(o, channel, with, "with");
	if (limit > 0) {
		json_object_set_new(o, "limit", json_integer(limit));
	}
	if (before && *before) {
		json_object_set_new(o, "before", json_string(before));
	}
	return finish(o);
}

char *zp_history_play(int seq, const char *channel, const char *with, const char *message) {
	json_t *o = request("history", seq);
	address(o, channel, with, "with");
	json_object_set_new(o, "play", json_string(message));
	return finish(o);
}

/* ---------- binary frames ---------- */

static void put_be32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static uint32_t get_be32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void zp_bin_header(uint8_t out[ZP_BIN_HDR], uint8_t kind, uint32_t id, uint32_t seq) {
	out[0] = kind;
	put_be32(out + 1, id);
	put_be32(out + 5, seq);
}

int zp_parse_bin(const uint8_t *b, size_t n, zp_bin *out) {
	if (n < ZP_BIN_HDR) {
		return -1;
	}
	out->kind = b[0];
	out->id = get_be32(b + 1);
	out->seq = get_be32(b + 5);
	out->data = b + ZP_BIN_HDR;
	out->len = n - ZP_BIN_HDR;
	return 0;
}
