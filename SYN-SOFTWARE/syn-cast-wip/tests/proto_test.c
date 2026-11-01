/* ------------------------------------------------------------------------
 *   Unit tests: SYN-CAST protocol v1's encoding, history on disk, voice
 *   files and the replay time-stretch. Exits non-zero on any failure.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "history.h"
#include "stretch.h"
#include "util.h"
#include "zproto.h"

#include <jansson.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures, checks;

#define CHECK(cond, ...)                                     \
	do {                                                     \
		checks++;                                            \
		if (!(cond)) {                                       \
			failures++;                                      \
			fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
			fprintf(stderr, __VA_ARGS__);                    \
			fputc('\n', stderr);                             \
		}                                                    \
	} while (0)

static json_t *parse_json(const char *s) {
	json_error_t e;
	return json_loads(s, 0, &e);
}

static void test_binary(void) {
	uint8_t pkt[ZP_BIN_HDR + 3];
	zp_bin_header(pkt, ZP_BIN_AUDIO, 0x01020304, 7);
	memcpy(pkt + ZP_BIN_HDR, "abc", 3);
	CHECK(pkt[0] == 1 && pkt[1] == 1 && pkt[2] == 2 && pkt[3] == 3 && pkt[4] == 4, "stream id big-endian");
	CHECK(pkt[5] == 0 && pkt[8] == 7, "packet seq big-endian");
	zp_bin b;
	CHECK(zp_parse_bin(pkt, sizeof pkt, &b) == 0 && b.kind == 1 && b.id == 0x01020304 && b.seq == 7 &&
		b.len == 3 && memcmp(b.data, "abc", 3) == 0, "binary parse");
	CHECK(zp_parse_bin(pkt, 8, &b) != 0, "short frame rejected");
	zp_bin_header(pkt, ZP_BIN_PIC, 9, 0);
	CHECK(zp_parse_bin(pkt, sizeof pkt, &b) == 0 && b.kind == ZP_BIN_PIC && b.id == 9 && b.seq == 0, "picture frame");
}

static void test_events(void) {
	zevent ev;
	const char *s;

	/* the hello answer */
	s = "{\"type\":\"ok\",\"seq\":1,\"member\":\"syntax990\",\"session\":\"syntax990/window/3\","
		"\"services\":[\"radio\",\"channels\"],\"channels\":[{\"channel\":\"ops\",\"name\":\"Ops\",\"kind\":\"open\","
		"\"members\":[\"syntax990\",\"m141\"],\"online\":[\"syntax990\",\"m141\"],\"talking\":null},"
		"{\"channel\":\"crew\",\"online\":[]}],\"held\":[{\"with\":\"m141\",\"count\":2},{\"with\":\"boz\",\"count\":1}],"
		"\"refused\":[{\"channel\":\"old\",\"code\":\"no_channel\"}],\"token\":\"sct1.p.m\",\"token_id\":\"abc123\","
		"\"limits\":{\"text\":4000}}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_REPLY && ev.seq == 1 && ev.ok, "hello ok");
	CHECK(strcmp(ev.member, "syntax990") == 0 && strcmp(ev.session, "syntax990/window/3") == 0, "member and session");
	CHECK(ev.n_channels == 2 && strcmp(ev.channels[0].id, "ops") == 0 && strcmp(ev.channels[0].name, "Ops") == 0 &&
		ev.channels[0].online == 2 && strcmp(ev.channels[1].name, "crew") == 0 && ev.channels[1].online == 0,
		"channels joined, online counted, a missing name falls back to the id");
	CHECK(strcmp(ev.channels[0].who[0], "syntax990") == 0 && strcmp(ev.channels[0].who[1], "m141") == 0,
		"who is online, by name");
	CHECK(ev.held == 3, "held direct messages summed: %d", ev.held);
	CHECK(ev.n_refused == 1 && strcmp(ev.refused[0].channel, "old") == 0 && strcmp(ev.refused[0].code, "no_channel") == 0,
		"refused channel");
	CHECK(strcmp(ev.token, "sct1.p.m") == 0 && strcmp(ev.token_id, "abc123") == 0, "the token an invite earned");
	zp_event_free(&ev);

	s = "{\"type\":\"error\",\"seq\":1,\"code\":\"unauthorized\",\"text\":\"bad token\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_REPLY && !ev.ok && strcmp(ev.code, "unauthorized") == 0 &&
		strcmp(ev.error, "bad token") == 0, "error reply");
	s = "{\"type\":\"error\",\"seq\":4,\"code\":\"busy\",\"text\":\"ops: m141 is talking\",\"talker\":\"m141\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && !ev.ok && strcmp(ev.code, "busy") == 0 && strcmp(ev.talker, "m141") == 0,
		"busy carries the talker");
	s = "{\"type\":\"error\",\"seq\":5,\"code\":\"forbidden\",\"service\":\"radio\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && strcmp(ev.service, "radio") == 0 && strcmp(ev.error, "forbidden") == 0,
		"forbidden names the service; the code stands in for a missing text");
	s = "{\"type\":\"error\",\"code\":\"revoked\",\"text\":\"this token was revoked\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_ERROR && strcmp(ev.code, "revoked") == 0,
		"an error without seq is the server's own");

	/* a join answer is one channel's record */
	s = "{\"type\":\"ok\",\"seq\":2,\"channel\":\"ops\",\"name\":\"Ops\",\"kind\":\"open\",\"members\":[\"a\"],"
		"\"online\":[\"a\",\"b\",\"c\"],\"talking\":\"b\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.n_channels == 1 && ev.channels[0].online == 3 &&
		strcmp(ev.channels[0].talking, "b") == 0, "join reply");

	s = "{\"type\":\"ok\",\"seq\":3,\"stream\":22457}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.stream == 22457, "talk reply");
	s = "{\"type\":\"ok\",\"seq\":4,\"stream\":22457,\"message\":\"20261101-0001ab\",\"ms\":1200,\"packets\":20}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && strcmp(ev.message, "20261101-0001ab") == 0 && ev.ms == 1200 &&
		ev.packets == 20, "over reply");
	s = "{\"type\":\"ok\",\"seq\":5,\"upload\":77}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.upload == 77, "pic reply");
	s = "{\"type\":\"ok\",\"seq\":6,\"channel\":\"ops\",\"messages\":[{\"id\":\"a\",\"kind\":\"text\"},{\"id\":\"b\"}],\"more\":true}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.messages && json_array_size(ev.messages) == 2 && ev.more,
		"history reply keeps the lines");
	zp_event_free(&ev);
	CHECK(ev.messages == NULL, "freed");

	/* events */
	s = "{\"type\":\"talk\",\"stream\":99,\"from\":\"alice\",\"channel\":\"ops\",\"codec\":\"opus\",\"rate\":16000,"
		"\"channels\":1,\"frame_ms\":60}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_TALK && ev.stream == 99 && strcmp(ev.from, "alice") == 0 &&
		ev.codec.rate == 16000 && ev.codec.frames_per_packet == 1 && ev.codec.frame_ms == 60 && !ev.replay &&
		ev.to[0] == 0, "talk event");
	s = "{\"type\":\"talk\",\"stream\":100,\"from\":\"alice\",\"to\":\"bob\",\"codec\":\"opus\",\"rate\":16000,"
		"\"channels\":1,\"frame_ms\":20,\"replay\":true,\"message\":\"m1\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.replay && strcmp(ev.to, "bob") == 0 && ev.codec.frame_ms == 20 &&
		strcmp(ev.message, "m1") == 0, "a replayed direct message");
	s = "{\"type\":\"talk\",\"stream\":101,\"from\":\"x\",\"channel\":\"ops\",\"frame_ms\":17}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.codec.frame_ms == 20 && ev.codec.rate == 16000,
		"an odd frame length falls back to 20 ms");
	s = "{\"type\":\"over\",\"stream\":99,\"from\":\"alice\",\"channel\":\"ops\",\"message\":\"m2\",\"ms\":600,\"reason\":\"stall\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_OVER && ev.stream == 99 && strcmp(ev.reason, "stall") == 0 &&
		ev.ms == 600, "over event");
	s = "{\"type\":\"say\",\"message\":\"m3\",\"from\":\"bob\",\"channel\":\"ops\",\"text\":\"h\\u00e9llo\",\"at\":\"2026-11-01T00:00:00Z\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_SAY && ev.text && strcmp(ev.text, "h\xc3\xa9llo") == 0 &&
		strcmp(ev.at, "2026-11-01T00:00:00Z") == 0 && ev.to[0] == 0, "say");
	zp_event_free(&ev);
	/* G228: a line spoken for a character, and the portrait reply */
	s = "{\"type\":\"say\",\"message\":\"m3a\",\"from\":\"kirk\",\"as\":\"data\",\"name\":\"Commander Data\","
		"\"colour\":\"#4fc3f7\",\"channel\":\"ops\",\"text\":\"x\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && strcmp(ev.from, "kirk") == 0 && strcmp(ev.as, "data") == 0 &&
		strcmp(ev.name, "Commander Data") == 0 && strcmp(ev.colour, "#4fc3f7") == 0, "say as a character");
	zp_event_free(&ev);
	s = "{\"type\":\"say\",\"message\":\"m3b\",\"from\":\"bob\",\"colour\":\"#ff8800\",\"channel\":\"ops\",\"text\":\"x\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.as[0] == 0 && ev.name[0] == 0 && strcmp(ev.colour, "#ff8800") == 0,
		"a plain speaker's colour");
	zp_event_free(&ev);
	s = "{\"type\":\"ok\",\"seq\":12,\"member\":\"data\",\"name\":\"Commander Data\",\"colour\":null,\"png\":\"iVBORw==\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_REPLY && ev.ok && ev.png && strcmp(ev.png, "iVBORw==") == 0 &&
		ev.colour[0] == 0, "portrait reply: the PNG kept, a null colour empty");
	zp_event_free(&ev);
	CHECK(ev.png == NULL, "the PNG freed");
	s = "{\"type\":\"say\",\"message\":\"m4\",\"from\":\"bob\",\"to\":\"me\",\"text\":\"x\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && strcmp(ev.to, "me") == 0 && ev.channel[0] == 0, "a direct message");
	zp_event_free(&ev);
	s = "{\"type\":\"loc\",\"message\":\"m5\",\"from\":\"c\",\"channel\":\"t\",\"lat\":51.5,\"lon\":-0.12,\"accuracy\":9,"
		"\"address\":\"London\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_LOC && fabs(ev.lat - 51.5) < 1e-9 &&
		fabs(ev.accuracy - 9) < 1e-9 && ev.text && strcmp(ev.text, "London") == 0, "location");
	zp_event_free(&ev);
	s = "{\"type\":\"pic\",\"message\":\"m6\",\"from\":\"c\",\"channel\":\"t\",\"mime\":\"image/jpeg\",\"width\":800,"
		"\"height\":600,\"size\":12345,\"blob\":41}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_PIC && ev.blob == 41 && ev.width == 800 && ev.size == 12345 &&
		strcmp(ev.mime, "image/jpeg") == 0, "picture event");
	s = "{\"type\":\"presence\",\"member\":\"m141\",\"status\":\"busy\",\"text\":\"at work\",\"devices\":2}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_PRESENCE && strcmp(ev.member, "m141") == 0 &&
		strcmp(ev.status, "busy") == 0 && ev.devices == 2 && ev.text && strcmp(ev.text, "at work") == 0, "presence");
	zp_event_free(&ev);
	s = "{\"type\":\"join\",\"channel\":\"ops\",\"member\":\"m141\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_JOIN && strcmp(ev.member, "m141") == 0 &&
		strcmp(ev.channel, "ops") == 0, "join event");
	s = "{\"type\":\"leave\",\"channel\":\"ops\",\"member\":\"m141\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_LEAVE, "leave event");
	s = "{\"type\":\"something_new\",\"x\":1}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_UNKNOWN, "unknown type");
	s = "[1,2]";
	CHECK(zp_parse(s, strlen(s), &ev) != 0, "array rejected");
	s = "{nope";
	CHECK(zp_parse(s, strlen(s), &ev) != 0, "bad json rejected");
}

static json_t *J(char *s) {
	json_t *o = parse_json(s);
	free(s);
	return o;
}

static const char *S(json_t *o, const char *k) {
	const char *v = json_string_value(json_object_get(o, k));
	return v ? v : "";
}

static void test_commands(void) {
	const char *chans[] = {"ops", "crew"};
	json_t *o = J(zp_hello(1, "sct1.a.b", NULL, "window", "syn-cast/0.2.0", chans, 2, false));
	CHECK(o && strcmp(S(o, "type"), "hello") == 0 && json_integer_value(json_object_get(o, "seq")) == 1 &&
		json_integer_value(json_object_get(o, "v")) == 1, "hello with v 1");
	CHECK(strcmp(S(o, "token"), "sct1.a.b") == 0 && !json_object_get(o, "invite"), "token, no invite");
	CHECK(strcmp(S(o, "device"), "window") == 0 && strcmp(S(o, "client"), "syn-cast/0.2.0") == 0, "device and client");
	CHECK(json_array_size(json_object_get(o, "channels")) == 2, "channels listed");
	json_decref(o);
	o = J(zp_hello(2, "sct1.a.b", "CODE", "", NULL, NULL, 0, true));
	CHECK(strcmp(S(o, "invite"), "CODE") == 0 && !json_object_get(o, "token"), "an invite replaces the token");
	CHECK(!json_object_get(o, "channels"), "no channels key: every channel the member belongs to");
	CHECK(strcmp(S(o, "device"), "window") == 0, "device defaults to window");
	json_decref(o);

	o = J(zp_join(3, "ops"));
	CHECK(strcmp(S(o, "type"), "join") == 0 && strcmp(S(o, "channel"), "ops") == 0, "join");
	json_decref(o);
	o = J(zp_leave(4, "ops", true));
	CHECK(strcmp(S(o, "type"), "leave") == 0 && json_is_true(json_object_get(o, "drop")), "leave for good");
	json_decref(o);
	o = J(zp_leave(5, "ops", false));
	CHECK(!json_object_get(o, "drop"), "leave this session only");
	json_decref(o);

	zp_codec c = {16000, 1, 60};
	o = J(zp_talk(6, "ops", NULL, &c));
	CHECK(strcmp(S(o, "type"), "talk") == 0 && strcmp(S(o, "channel"), "ops") == 0 && !json_object_get(o, "to"),
		"talk in a channel");
	CHECK(strcmp(S(o, "codec"), "opus") == 0 && json_integer_value(json_object_get(o, "rate")) == 16000 &&
		json_integer_value(json_object_get(o, "channels")) == 1 && json_integer_value(json_object_get(o, "frame_ms")) == 60,
		"opus 16 kHz mono 60 ms");
	json_decref(o);
	zp_codec two = {16000, 2, 20};
	o = J(zp_talk(7, NULL, "m141", &two));
	CHECK(strcmp(S(o, "to"), "m141") == 0 && !json_object_get(o, "channel") &&
		json_integer_value(json_object_get(o, "frame_ms")) == 40, "a direct talk; two 20 ms frames are 40 ms");
	json_decref(o);
	o = J(zp_over(8, 22457));
	CHECK(strcmp(S(o, "type"), "over") == 0 && json_integer_value(json_object_get(o, "stream")) == 22457, "over");
	json_decref(o);

	o = J(zp_say(9, "ops", NULL, "h\xc3\xa9llo"));
	CHECK(strcmp(S(o, "type"), "say") == 0 && strcmp(S(o, "text"), "h\xc3\xa9llo") == 0 &&
		strcmp(S(o, "channel"), "ops") == 0, "say keeps UTF-8");
	json_decref(o);
	o = J(zp_portrait(11, "data"));
	CHECK(strcmp(S(o, "type"), "portrait") == 0 && strcmp(S(o, "member"), "data") == 0 &&
		json_integer_value(json_object_get(o, "seq")) == 11, "portrait asks for one member");
	json_decref(o);
	o = J(zp_pic(10, "ops", NULL, "image/jpeg", 640, 400, 12345));
	CHECK(strcmp(S(o, "type"), "pic") == 0 && strcmp(S(o, "mime"), "image/jpeg") == 0 &&
		json_integer_value(json_object_get(o, "width")) == 640 && json_integer_value(json_object_get(o, "height")) == 400 &&
		json_integer_value(json_object_get(o, "size")) == 12345, "pic reserves an upload");
	json_decref(o);
	o = J(zp_loc(11, "ops", NULL, 51.5, -0.12, 7, "London"));
	CHECK(strcmp(S(o, "type"), "loc") == 0 && fabs(json_number_value(json_object_get(o, "lat")) - 51.5) < 1e-9 &&
		fabs(json_number_value(json_object_get(o, "lon")) + 0.12) < 1e-9 &&
		fabs(json_number_value(json_object_get(o, "accuracy")) - 7) < 1e-9 && strcmp(S(o, "address"), "London") == 0, "loc");
	json_decref(o);
	o = J(zp_loc(12, "ops", NULL, 1, 2, 0, ""));
	CHECK(!json_object_get(o, "address"), "an empty address is left out");
	json_decref(o);
	o = J(zp_presence(13, "busy", NULL));
	CHECK(strcmp(S(o, "type"), "presence") == 0 && strcmp(S(o, "status"), "busy") == 0 && !json_object_get(o, "text"),
		"presence busy");
	json_decref(o);
	o = J(zp_history(14, "ops", NULL, 20, "m9"));
	CHECK(strcmp(S(o, "type"), "history") == 0 && strcmp(S(o, "channel"), "ops") == 0 &&
		json_integer_value(json_object_get(o, "limit")) == 20 && strcmp(S(o, "before"), "m9") == 0, "history page");
	json_decref(o);
	o = J(zp_history(15, NULL, "m141", 0, NULL));
	CHECK(strcmp(S(o, "with"), "m141") == 0 && !json_object_get(o, "channel") && !json_object_get(o, "limit") &&
		!json_object_get(o, "before"), "the direct messages with a member, the server's default page");
	json_decref(o);
	o = J(zp_history_play(16, "ops", NULL, "m7"));
	CHECK(strcmp(S(o, "type"), "history") == 0 && strcmp(S(o, "play"), "m7") == 0, "history play");
	json_decref(o);
}

static void test_util(void) {
	const uint8_t raw[] = {0xfb, 0xff, 0x00, 'a'};
	char *s = b64_encode(raw, sizeof raw, false);
	uint8_t back[8];
	CHECK(b64_decode(s, back, sizeof back) == 4 && memcmp(back, raw, 4) == 0, "base64 round trip");
	free(s);
	s = b64_encode(raw, sizeof raw, true);
	CHECK(!strchr(s, '=') && !strchr(s, '+') && !strchr(s, '/'), "base64url alphabet");
	CHECK(b64_decode(s, back, sizeof back) == 4 && memcmp(back, raw, 4) == 0, "base64url round trip");
	free(s);
	uint32_t cp;
	CHECK(utf8_next("\xe2\x82\xac", 3, &cp) == 3 && cp == 0x20AC, "utf8 euro");
	CHECK(utf8_next("\xe2\x82", 2, &cp) == 1 && cp == 0xFFFD, "utf8 truncated");
	CHECK(utf8_prev_len("a\xe2\x82\xac", 4) == 3, "utf8 prev");
	char dst[4];
	str_copy(dst, sizeof dst, "a\xe2\x82\xac");
	CHECK(strcmp(dst, "a") == 0, "str_copy keeps UTF-8 whole");
}

static char tmpdir[256];

static void test_history(void) {
	history h;
	CHECK(history_open(&h, tmpdir, "Chan/../x") == 0, "history open");
	CHECK(strrchr(h.dir, '/') == h.dir + strlen(tmpdir) && strcmp(strrchr(h.dir, '/'), "/..") != 0,
		"channel name is one directory inside the data dir: %s", h.dir);
	history dots;
	CHECK(history_open(&dots, tmpdir, "..") == 0 && strcmp(strrchr(dots.dir, '/'), "/_.") == 0,
		"\"..\" becomes a plain name: %s", dots.dir);
	history_close(&dots);
	hmsg a = {.kind = HM_TEXT, .text = "first"};
	str_copy(a.from, sizeof a.from, "alice");
	uint64_t ida = history_add(&h, &a);
	hmsg v = {.kind = HM_VOICE, .partial = true};
	str_copy(v.from, sizeof v.from, "bob");
	uint64_t idv = history_add(&h, &v);
	hmsg old = {.kind = HM_EVENT, .ts = wall_ms() - 40LL * 86400000, .text = "ancient"};
	history_add(&h, &old);
	CHECK(ida && idv && ida != idv, "ids unique");
	hmsg *m = history_find(&h, idv);
	m->partial = false;
	m->duration_ms = 1200;
	history_set_text(m, "transcribed");
	history_commit(&h, m);
	hmsg c = {.kind = HM_TEXT, .text = "for the captain"};
	str_copy(c.from, sizeof c.from, "kirk");
	hmsg_speaker(&c, "data", "Commander Data", "#4fc3f7");
	uint64_t idc = history_add(&h, &c);
	hmsg p = {.kind = HM_TEXT, .text = "plain"};
	str_copy(p.from, sizeof p.from, "bob");
	hmsg_speaker(&p, "", "Bob B", "#ff8800");
	uint64_t idp = history_add(&h, &p);
	CHECK(strcmp(hmsg_who(&c), "Commander Data") == 0 && strcmp(hmsg_who(&p), "bob") == 0 && p.name[0] == 0,
		"a character's name shown; a name without a character is not kept");
	history_close(&h);
	CHECK(history_open(&h, tmpdir, "Chan/../x") == 0 && (m = history_find(&h, idc)) && strcmp(m->as, "data") == 0 &&
		strcmp(m->name, "Commander Data") == 0 && strcmp(m->colour, "#4fc3f7") == 0 &&
		(m = history_find(&h, idp)) && m->as[0] == 0 && m->name[0] == 0 && strcmp(m->colour, "#ff8800") == 0,
		"the speaker survives a reopen");
	history_close(&h);

	CHECK(history_open(&h, tmpdir, "Chan/../x") == 0 && h.n == 5, "reopen keeps 5, got %zu", h.n);
	m = history_find(&h, idv);
	CHECK(m && !m->partial && m->duration_ms == 1200 && m->text && strcmp(m->text, "transcribed") == 0,
		"later record replaces the earlier");
	CHECK(history_prune(&h, 30, 0) == 1 && h.n == 4, "prune drops the 40-day-old message");
	history_close(&h);
	CHECK(history_open(&h, tmpdir, "Chan/../x") == 0 && h.n == 4, "prune persisted");
	history_close(&h);
}

static void test_voice_file(void) {
	char path[300];
	snprintf(path, sizeof path, "%s/v.spt", tmpdir);
	hv_writer w;
	zp_codec c = {16000, 1, 60};
	CHECK(hv_open(&w, path, &c) == 0, "hv_open");
	hv_write(&w, (const uint8_t *)"abc", 3);
	hv_write(&w, NULL, 0);
	hv_write(&w, (const uint8_t *)"de", 2);
	CHECK(hv_duration_ms(&w) == 180, "duration counts lost packets");
	hv_close(&w);
	hv_file f;
	CHECK(hv_load(path, &f) == 0 && f.n == 3 && f.p[0].len == 3 && f.p[1].len == 0 && f.p[2].len == 2 &&
		memcmp(f.p[2].data, "de", 2) == 0 && f.codec.frame_ms == 60, "voice file round trip");
	hv_free(&f);
}

static int crossings(const int16_t *s, size_t n) {
	int z = 0;
	for (size_t i = 1; i < n; i++) {
		if ((s[i - 1] < 0) != (s[i] < 0)) z++;
	}
	return z;
}

static void test_stretch(void) {
	const size_t n = 48000;
	int16_t *in = malloc(n * sizeof *in);
	for (size_t i = 0; i < n; i++) {
		in[i] = (int16_t)(8000 * sin(2 * M_PI * 440 * (double)i / 48000));
	}
	int16_t *out = malloc(n * 2 * sizeof *out);
	stretch *s = stretch_new();
	stretch_reset(s, 1.5);
	size_t got = 0;
	for (size_t off = 0; off < n; off += 960) {
		got += stretch_process(s, in + off, 960, out + got, n * 2 - got);
	}
	size_t f;
	while ((f = stretch_flush(s, out + got, n * 2 - got)) > 0) got += f;
	double ratio = (double)got / n;
	CHECK(ratio > 0.62 && ratio < 0.72, "1.5x keeps %.3f of the length, want ~0.667", ratio);
	/* pitch: same crossings per second in the middle */
	double hz = crossings(out + 4800, got - 9600) / 2.0 / ((double)(got - 9600) / 48000);
	CHECK(fabs(hz - 440) < 15, "pitch kept: %.1f Hz", hz);
	stretch_reset(s, 1.0);
	CHECK(stretch_process(s, in, 100, out, 100) == 100 && memcmp(in, out, 200) == 0, "1x is a copy");
	stretch_free(s);
	free(in);
	free(out);
}

int main(void) {
	snprintf(tmpdir, sizeof tmpdir, "/tmp/syn-cast-test-XXXXXX");
	if (!mkdtemp(tmpdir)) {
		perror("mkdtemp");
		return 1;
	}
	test_binary();
	test_events();
	test_commands();
	test_util();
	test_history();
	test_voice_file();
	test_stretch();
	char cmd[300];
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmpdir);
	if (system(cmd) != 0) {
		fprintf(stderr, "could not remove %s\n", tmpdir);
	}
	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
