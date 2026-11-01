/* ------------------------------------------------------------------------
 *   Unit tests: the Channel API encoding, the token, history on disk, voice
 *   files and the replay time-stretch. Exits non-zero on any failure.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "history.h"
#include "jwt.h"
#include "stretch.h"
#include "util.h"
#include "zproto.h"

#include <jansson.h>
#include <math.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
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

static void test_codec(void) {
	zp_codec c = {16000, 1, 60};
	char hdr[16];
	zp_codec_encode(&c, hdr);
	CHECK(strcmp(hdr, "gD4BPA==") == 0, "codec header %s, want the reference's gD4BPA==", hdr);
	zp_codec d;
	CHECK(zp_codec_decode("gD4BPA==", &d) == 0 && d.rate == 16000 && d.frames_per_packet == 1 && d.frame_ms == 60,
		"decode gD4BPA==");
	/* what the phone app announces by default: 16 kHz, 2 frames of 60 ms */
	c = (zp_codec){16000, 2, 60};
	zp_codec_encode(&c, hdr);
	CHECK(zp_codec_decode(hdr, &d) == 0 && d.frames_per_packet == 2, "2-frame header round trip");
	CHECK(zp_codec_decode("", &d) != 0, "empty header rejected");
	CHECK(zp_codec_decode("AAAAAA==", &d) != 0, "zero rate rejected");
}

static void test_binary(void) {
	uint8_t pkt[ZP_BIN_HDR + 3];
	zp_bin_header(pkt, ZP_BIN_AUDIO, 0x01020304, 7);
	memcpy(pkt + ZP_BIN_HDR, "abc", 3);
	CHECK(pkt[0] == 1 && pkt[1] == 1 && pkt[2] == 2 && pkt[3] == 3 && pkt[4] == 4, "stream id big-endian");
	CHECK(pkt[5] == 0 && pkt[8] == 7, "packet id big-endian");
	zp_bin b;
	CHECK(zp_parse_bin(pkt, sizeof pkt, &b) == 0 && b.type == 1 && b.id == 0x01020304 && b.sub == 7 &&
		b.len == 3 && memcmp(b.data, "abc", 3) == 0, "binary parse");
	CHECK(zp_parse_bin(pkt, 8, &b) != 0, "short packet rejected");
}

static void test_events(void) {
	zevent ev;
	const char *s;

	s = "{\"seq\":3,\"success\":true,\"refresh_token\":\"RT\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_REPLY && ev.seq == 3 && ev.success &&
		strcmp(ev.refresh_token, "RT") == 0, "logon reply");
	s = "{\"seq\":4,\"success\":true,\"stream_id\":22457}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.stream_id == 22457, "start_stream reply");
	s = "{\"seq\":5,\"error\":\"channel is not ready\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_REPLY && !ev.success &&
		strcmp(ev.error, "channel is not ready") == 0, "error reply");

	s = "{\"command\":\"on_channel_status\",\"channel\":\"test\",\"status\":\"online\",\"users_online\":3,"
		"\"images_supported\":true,\"texting_supported\":true,\"locations_supported\":false}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_CHANNEL_STATUS && ev.online && ev.users_online == 3 &&
		ev.images_ok && ev.texting_ok && !ev.locations_ok && strcmp(ev.channel, "test") == 0, "channel status");

	s = "{\"command\":\"on_stream_start\",\"type\":\"audio\",\"codec\":\"opus\",\"codec_header\":\"gD4CPA==\","
		"\"packet_duration\":120,\"stream_id\":99,\"channel\":\"test\",\"from\":\"alice\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_STREAM_START && ev.stream_id == 99 &&
		ev.codec_hdr.frames_per_packet == 2 && ev.packet_duration == 120 && strcmp(ev.from, "alice") == 0 &&
		ev.forwho[0] == 0, "stream start");

	s = "{\"command\":\"on_text_message\",\"channel\":\"test\",\"from\":\"bob\",\"for\":false,\"message_id\":7,"
		"\"text\":\"h\\u00e9llo\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_TEXT && ev.text && strcmp(ev.text, "h\xc3\xa9llo") == 0 &&
		ev.forwho[0] == 0 && ev.message_id == 7, "text, for:false");
	zp_event_free(&ev);
	s = "{\"command\":\"on_text_message\",\"channel\":\"test\",\"from\":\"bob\",\"for\":\"me\",\"text\":\"x\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && strcmp(ev.forwho, "me") == 0, "private text");
	zp_event_free(&ev);

	s = "{\"command\":\"on_location\",\"channel\":\"t\",\"from\":\"c\",\"latitude\":51.5,\"longitude\":-0.12,"
		"\"rgl\":\"London\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_LOCATION && fabs(ev.latitude - 51.5) < 1e-9 &&
		ev.text && strcmp(ev.text, "London") == 0, "location with the example's rgl key");
	zp_event_free(&ev);

	s = "{\"command\":\"on_image\",\"channel\":\"t\",\"from\":\"c\",\"message_id\":41,\"type\":\"jpeg\","
		"\"width\":800,\"height\":600,\"source\":\"camera\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_IMAGE && ev.message_id == 41 && ev.width == 800,
		"image");
	s = "{\"command\":\"on_stream_stop\",\"stream_id\":99}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_STREAM_STOP && ev.stream_id == 99, "stream stop");
	s = "{\"command\":\"on_error\",\"error\":\"server closed connection\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_ERROR, "error event");
	s = "{\"command\":\"on_something_new\"}";
	CHECK(zp_parse(s, strlen(s), &ev) == 0 && ev.kind == ZEV_UNKNOWN, "unknown command");
	s = "[1,2]";
	CHECK(zp_parse(s, strlen(s), &ev) != 0, "array rejected");
	s = "{nope";
	CHECK(zp_parse(s, strlen(s), &ev) != 0, "bad json rejected");
}

static void test_commands(void) {
	const char *chans[] = {"one", "two"};
	char *s = zp_logon(1, "TOKEN", NULL, "user", "pw", chans, 2, false, false);
	json_t *o = parse_json(s);
	CHECK(o && strcmp(json_string_value(json_object_get(o, "command")), "logon") == 0, "logon command");
	CHECK(strcmp(json_string_value(json_object_get(o, "auth_token")), "TOKEN") == 0, "auth_token");
	CHECK(json_array_size(json_object_get(o, "channels")) == 2, "channels array");
	CHECK(!json_object_get(o, "refresh_token") && !json_object_get(o, "listen_only"), "no refresh, not listen-only");
	json_decref(o);
	free(s);

	s = zp_logon(2, "TOKEN", "REFRESH", "user", "pw", chans, 1, true, true);
	o = parse_json(s);
	CHECK(!json_object_get(o, "auth_token") && json_object_get(o, "refresh_token"), "refresh replaces auth_token");
	CHECK(json_is_true(json_object_get(o, "listen_only")), "listen_only");
	CHECK(json_is_true(json_object_get(json_object_get(o, "features"), "transcriptions")), "transcriptions feature");
	json_decref(o);
	free(s);

	zp_codec c = {16000, 1, 60};
	s = zp_start_stream(3, "one", NULL, &c);
	o = parse_json(s);
	CHECK(strcmp(json_string_value(json_object_get(o, "codec_header")), "gD4BPA==") == 0, "stream codec header");
	CHECK(json_integer_value(json_object_get(o, "packet_duration")) == 60, "packet_duration");
	CHECK(!json_object_get(o, "for"), "no for on a channel stream");
	json_decref(o);
	free(s);

	s = zp_send_text(4, "one", "bob", "hi");
	o = parse_json(s);
	CHECK(strcmp(json_string_value(json_object_get(o, "for")), "bob") == 0, "private text for");
	json_decref(o);
	free(s);

	s = zp_send_image(5, "one", "", 640, 480, 100, 2000);
	o = parse_json(s);
	CHECK(json_integer_value(json_object_get(o, "thumbnail_content_length")) == 100 &&
		json_integer_value(json_object_get(o, "content_length")) == 2000 &&
		strcmp(json_string_value(json_object_get(o, "type")), "jpeg") == 0, "send_image");
	json_decref(o);
	free(s);

	s = zp_send_location(6, "one", NULL, 1.5, 2.5, 10, "Somewhere");
	o = parse_json(s);
	CHECK(json_real_value(json_object_get(o, "latitude")) == 1.5 &&
		strcmp(json_string_value(json_object_get(o, "formatted_address")), "Somewhere") == 0, "send_location");
	json_decref(o);
	free(s);
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
	history_close(&h);

	CHECK(history_open(&h, tmpdir, "Chan/../x") == 0 && h.n == 3, "reopen keeps 3, got %zu", h.n);
	m = history_find(&h, idv);
	CHECK(m && !m->partial && m->duration_ms == 1200 && m->text && strcmp(m->text, "transcribed") == 0,
		"later record replaces the earlier");
	CHECK(history_prune(&h, 30, 0) == 1 && h.n == 2, "prune drops the 40-day-old message");
	history_close(&h);
	CHECK(history_open(&h, tmpdir, "Chan/../x") == 0 && h.n == 2, "prune persisted");
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

static void test_jwt(void) {
	char key[300];
	snprintf(key, sizeof key, "%s/key.pem", tmpdir);
	EVP_PKEY *pk = EVP_RSA_gen(2048);
	FILE *f = fopen(key, "w");
	PEM_write_PrivateKey(f, pk, NULL, NULL, 0, NULL, NULL);
	fclose(f);
	char err[200];
	char *tok = jwt_mint(key, "ISSUER", 60, err, sizeof err);
	CHECK(tok != NULL, "jwt minted: %s", err);
	if (tok) {
		char *dot1 = strchr(tok, '.'), *dot2 = dot1 ? strchr(dot1 + 1, '.') : NULL;
		CHECK(dot1 && dot2, "three parts");
		if (dot1 && dot2) {
			uint8_t body[256];
			*dot2 = 0;
			long n = b64_decode(dot1 + 1, body, sizeof body - 1);
			body[n > 0 ? n : 0] = 0;
			CHECK(strstr((char *)body, "\"iss\":\"ISSUER\"") != NULL, "payload carries the issuer: %s", body);
			uint8_t sig[512];
			long sn = b64_decode(dot2 + 1, sig, sizeof sig);
			EVP_MD_CTX *ctx = EVP_MD_CTX_new();
			int ok = EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pk) == 1 &&
				EVP_DigestVerify(ctx, sig, (size_t)sn, (uint8_t *)tok, strlen(tok)) == 1;
			EVP_MD_CTX_free(ctx);
			CHECK(ok, "RS256 signature verifies with the public key");
		}
		free(tok);
	}
	CHECK(jwt_mint("/nonexistent", "I", 60, err, sizeof err) == NULL, "missing key fails");
	EVP_PKEY_free(pk);
}

int main(void) {
	snprintf(tmpdir, sizeof tmpdir, "/tmp/syn-cast-test-XXXXXX");
	if (!mkdtemp(tmpdir)) {
		perror("mkdtemp");
		return 1;
	}
	test_codec();
	test_binary();
	test_events();
	test_commands();
	test_util();
	test_history();
	test_voice_file();
	test_stretch();
	test_jwt();
	char cmd[300];
	snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmpdir);
	if (system(cmd) != 0) {
		fprintf(stderr, "could not remove %s\n", tmpdir);
	}
	printf("%d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
