/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "audio.h"
#include "util.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <pulse/error.h>
#include <pulse/simple.h>
#include <rnnoise.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

const zp_codec PTT_CODEC = {16000, 1, FRAME_MS};

bool audio_null(void) {
	const char *e = getenv("SYN_CAST_NOAUDIO");
	return e && *e && strcmp(e, "0") != 0;
}

static int level_of(const int16_t *pcm, size_t n) {
	if (n == 0) {
		return 0;
	}
	double sum = 0;
	for (size_t k = 0; k < n; k++) {
		sum += (double)pcm[k] * pcm[k];
	}
	double rms = sqrt(sum / (double)n);
	if (rms < 1) {
		return 0;
	}
	/* -60 dBFS .. 0 dBFS onto 0..100 */
	double db = 20 * log10(rms / 32768.0);
	int v = (int)((db + 60) * 100 / 60);
	return v < 0 ? 0 : v > 100 ? 100 : v;
}

static void apply_gain(int16_t *pcm, size_t n, float g) {
	if (g == 1.0f) {
		return;
	}
	for (size_t k = 0; k < n; k++) {
		float v = pcm[k] * g;
		pcm[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
}

static void sleep_until(struct timespec *t, long add_ns) {
	t->tv_nsec += add_ns;
	while (t->tv_nsec >= 1000000000L) {
		t->tv_nsec -= 1000000000L;
		t->tv_sec++;
	}
	while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, t, NULL) == EINTR) {
	}
}

/* ---------- playback ---------- */

#define RING_CAP (AUDIO_RATE * 4)
#define CHUNK (AUDIO_RATE / 50)   /* 20 ms */

static struct {
	pthread_t thread;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	int16_t ring[RING_CAP];
	size_t head, len;
	uint64_t written, played;
	bool quit, flush, running;
	_Atomic int level;
	_Atomic float gain;
} pb = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER, .gain = 1.0f};

static void *playback_thread(void *arg) {
	(void)arg;
	bool null_sink = audio_null();
	pa_simple *pa = NULL;
	FILE *dump = NULL;
	if (null_sink) {
		const char *p = getenv("SYN_CAST_PLAYOUT");
		if (p && *p) {
			dump = fopen(p, "wb");
		}
	}
	struct timespec clock;
	clock_gettime(CLOCK_MONOTONIC, &clock);
	int16_t chunk[CHUNK];
	for (;;) {
		pthread_mutex_lock(&pb.mu);
		while (!pb.quit && pb.len == 0 && !pb.flush) {
			pthread_cond_wait(&pb.cv, &pb.mu);
		}
		if (pb.quit) {
			pthread_mutex_unlock(&pb.mu);
			break;
		}
		bool flush = pb.flush;
		pb.flush = false;
		size_t n = pb.len < CHUNK ? pb.len : CHUNK;
		for (size_t k = 0; k < n; k++) {
			chunk[k] = pb.ring[(pb.head + k) % RING_CAP];
		}
		pb.head = (pb.head + n) % RING_CAP;
		pb.len -= n;
		pb.played += n;
		pthread_mutex_unlock(&pb.mu);

		if (flush && pa) {
			pa_simple_flush(pa, NULL);
		}
		if (n == 0) {
			continue;
		}
		apply_gain(chunk, n, pb.gain);
		pb.level = level_of(chunk, n);

		if (null_sink) {
			if (dump) {
				fwrite(chunk, 2, n, dump);
				fflush(dump);
			}
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			if (now.tv_sec > clock.tv_sec + 1) {
				clock = now; /* idle gap: restart the clock */
			}
			sleep_until(&clock, (long)n * 1000000000L / AUDIO_RATE);
			continue;
		}
		if (!pa) {
			pa_sample_spec ss = {.format = PA_SAMPLE_S16LE, .rate = AUDIO_RATE, .channels = 1};
			pa_buffer_attr ba = {
				.maxlength = (uint32_t)-1,
				.tlength = AUDIO_RATE * 2 / 10 * 2,  /* 200 ms */
				.prebuf = (uint32_t)-1,
				.minreq = (uint32_t)-1,
				.fragsize = (uint32_t)-1,
			};
			int err = 0;
			pa = pa_simple_new(NULL, "syn-cast", PA_STREAM_PLAYBACK, NULL, "incoming voice", &ss, NULL, &ba, &err);
			if (!pa) {
				LOG("playback: %s", pa_strerror(err));
				usleep(500000);
				continue;
			}
		}
		int err = 0;
		if (pa_simple_write(pa, chunk, n * 2, &err) < 0) {
			LOG("playback: %s", pa_strerror(err));
			pa_simple_free(pa);
			pa = NULL;
		}
	}
	if (pa) {
		pa_simple_drain(pa, NULL);
		pa_simple_free(pa);
	}
	if (dump) {
		fclose(dump);
	}
	return NULL;
}

int playback_start(void) {
	pb.quit = false;
	if (pthread_create(&pb.thread, NULL, playback_thread, NULL) != 0) {
		return -1;
	}
	pb.running = true;
	return 0;
}

void playback_stop(void) {
	if (!pb.running) {
		return;
	}
	pthread_mutex_lock(&pb.mu);
	pb.quit = true;
	pthread_cond_signal(&pb.cv);
	pthread_mutex_unlock(&pb.mu);
	pthread_join(pb.thread, NULL);
	pb.running = false;
}

size_t playback_write(const int16_t *pcm, size_t n) {
	pthread_mutex_lock(&pb.mu);
	size_t room = RING_CAP - pb.len;
	if (n > room) {
		n = room;
	}
	for (size_t k = 0; k < n; k++) {
		pb.ring[(pb.head + pb.len + k) % RING_CAP] = pcm[k];
	}
	pb.len += n;
	pb.written += n;
	pthread_cond_signal(&pb.cv);
	pthread_mutex_unlock(&pb.mu);
	return n;
}

size_t playback_queued(void) {
	pthread_mutex_lock(&pb.mu);
	size_t n = pb.len;
	pthread_mutex_unlock(&pb.mu);
	return n;
}

uint64_t playback_written(void) {
	pthread_mutex_lock(&pb.mu);
	uint64_t n = pb.written;
	pthread_mutex_unlock(&pb.mu);
	return n;
}

uint64_t playback_played(void) {
	pthread_mutex_lock(&pb.mu);
	uint64_t n = pb.played;
	pthread_mutex_unlock(&pb.mu);
	return n;
}

int playback_level(void) {
	return pb.len ? pb.level : 0;
}

void playback_set_gain_db(double db) {
	pb.gain = (float)pow(10.0, db / 20.0);
}

void playback_flush(void) {
	pthread_mutex_lock(&pb.mu);
	pb.played += pb.len;
	pb.len = 0;
	pb.flush = true;
	pthread_cond_signal(&pb.cv);
	pthread_mutex_unlock(&pb.mu);
}

/* ---------- capture ---------- */

#define VOX_PREROLL 5          /* packets kept before speech: 300 ms */
#define VOX_HANG_MS 1200
#define VOX_ATTACK 2           /* frames above threshold to open */

static struct {
	pthread_t thread;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	int pipe_wr;
	capture_opts o;
	bool want, quit, running;
	/* every capture_begin() is answered with exactly one CAP_END (or
	 * CAP_ERROR), even a tap shorter than one frame */
	unsigned begins;
	_Atomic bool vox;
	_Atomic int level;
} cap = {.mu = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER};

static void cap_send(uint8_t kind, uint8_t level, const uint8_t *data, size_t len) {
	uint8_t buf[sizeof(cap_hdr) + MAX_OPUS_PACKET];
	cap_hdr h = {kind, level, (uint16_t)len};
	memcpy(buf, &h, sizeof h);
	if (len) {
		memcpy(buf + sizeof h, data, len);
	}
	size_t total = sizeof h + len;
	while (write(cap.pipe_wr, buf, total) < 0 && errno == EINTR) {
	}
}

typedef struct {
	uint8_t data[MAX_OPUS_PACKET];
	int len;
	uint8_t level;
} held_packet;

static void *capture_thread(void *arg) {
	(void)arg;
	bool null_mic = audio_null();
	OpusEncoder *enc = ptt_encoder_create();
	DenoiseState *dn = rnnoise_create(NULL);
	int16_t pcm[FRAME_SAMPLES];
	float fbuf[480];
	uint8_t pkt[MAX_OPUS_PACKET];
	held_packet preroll[VOX_PREROLL];
	double tone_phase = 0;
	unsigned served = 0;

	for (;;) {
		pthread_mutex_lock(&cap.mu);
		while (!cap.quit && !cap.want && !cap.vox && cap.begins == served) {
			pthread_cond_wait(&cap.cv, &cap.mu);
		}
		bool quit = cap.quit;
		pthread_mutex_unlock(&cap.mu);
		if (quit) {
			break;
		}

		pa_simple *pa = NULL;
		if (!null_mic) {
			pa_sample_spec ss = {.format = PA_SAMPLE_S16LE, .rate = AUDIO_RATE, .channels = 1};
			pa_buffer_attr ba = {
				.maxlength = (uint32_t)-1,
				.fragsize = AUDIO_RATE / 50 * 2,   /* 20 ms */
			};
			int err = 0;
			pa = pa_simple_new(NULL, "syn-cast", PA_STREAM_RECORD, NULL, "push-to-talk", &ss, NULL, &ba, &err);
			if (!pa) {
				LOG("microphone: %s", pa_strerror(err));
				cap_send(CAP_ERROR, 0, NULL, 0);
				pthread_mutex_lock(&cap.mu);
				cap.want = false;
				cap.vox = false;
				served = cap.begins;
				pthread_mutex_unlock(&cap.mu);
				continue;
			}
		}
		opus_encoder_ctl(enc, OPUS_RESET_STATE);
		struct timespec clock;
		clock_gettime(CLOCK_MONOTONIC, &clock);
		float gain = (float)pow(10.0, cap.o.gain_db / 20.0);
		bool talking = false, by_ptt = false;
		int held = 0, attack = 0;
		int64_t last_voice = 0;

		for (;;) {
			if (null_mic) {
				sleep_until(&clock, (long)FRAME_MS * 1000000L);
				for (int k = 0; k < FRAME_SAMPLES; k++) {
					pcm[k] = (int16_t)(3277 * sin(tone_phase));
					tone_phase += 2 * M_PI * 600 / AUDIO_RATE;
				}
				tone_phase = fmod(tone_phase, 2 * M_PI);
			} else {
				int err = 0;
				if (pa_simple_read(pa, pcm, sizeof pcm, &err) < 0) {
					LOG("microphone: %s", pa_strerror(err));
					cap_send(CAP_ERROR, 0, NULL, 0);
					pthread_mutex_lock(&cap.mu);
					cap.want = false;
					served = cap.begins;
					pthread_mutex_unlock(&cap.mu);
					break;
				}
			}
			apply_gain(pcm, FRAME_SAMPLES, gain);
			/* the meter shows what the mic hears, before noise suppression */
			int level = level_of(pcm, FRAME_SAMPLES);
			cap.level = level;
			float vad = 0;
			if (cap.o.noise_suppression || cap.vox) {
				for (int off = 0; off < FRAME_SAMPLES; off += 480) {
					for (int k = 0; k < 480; k++) {
						fbuf[k] = pcm[off + k];
					}
					float p = rnnoise_process_frame(dn, fbuf, fbuf);
					vad = p > vad ? p : vad;
					if (cap.o.noise_suppression) {
						for (int k = 0; k < 480; k++) {
							float v = fbuf[k];
							pcm[off + k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
						}
					}
				}
			}
			if (null_mic) {
				vad = 1; /* the test tone counts as a voice */
			}
			int n = opus_encode(enc, pcm, FRAME_SAMPLES, pkt, sizeof pkt);
			if (n < 0) {
				n = 0;
			}

			pthread_mutex_lock(&cap.mu);
			bool want = cap.want, vox = cap.vox, quit2 = cap.quit;
			bool pressed = cap.begins != served;
			served = cap.begins;
			pthread_mutex_unlock(&cap.mu);

			if (pressed && talking && !by_ptt) {
				/* PTT pressed during a VOX transmission: the press owns it now */
				by_ptt = true;
			} else if (pressed && !talking) {
				talking = by_ptt = true;
				held = 0;
			}
			if (talking) {
				if (n > 0) {
					cap_send(CAP_PACKET, (uint8_t)level, pkt, (size_t)n);
				}
				if (by_ptt && !want) {
					cap_send(CAP_END, 0, NULL, 0);
					talking = by_ptt = false;
				} else if (!by_ptt) {
					if (vad >= cap.o.vox_threshold) {
						last_voice = now_ms();
					} else if (now_ms() - last_voice > VOX_HANG_MS) {
						cap_send(CAP_END, 0, NULL, 0);
						talking = false;
					}
				}
			} else if (vox && !want) {
				attack = vad >= cap.o.vox_threshold ? attack + 1 : 0;
				if (attack >= VOX_ATTACK) {
					cap_send(CAP_VOX_START, (uint8_t)level, NULL, 0);
					for (int k = 0; k < held; k++) {
						cap_send(CAP_PACKET, preroll[k].level, preroll[k].data, (size_t)preroll[k].len);
					}
					held = 0;
					if (n > 0) {
						cap_send(CAP_PACKET, (uint8_t)level, pkt, (size_t)n);
					}
					talking = true;
					by_ptt = false;
					last_voice = now_ms();
				} else if (n > 0) {
					if (held == VOX_PREROLL) {
						memmove(preroll, preroll + 1, sizeof preroll[0] * (VOX_PREROLL - 1));
						held--;
					}
					memcpy(preroll[held].data, pkt, (size_t)n);
					preroll[held].len = n;
					preroll[held].level = (uint8_t)level;
					held++;
				}
			}
			if (quit2 || (!talking && !want && !vox)) {
				if (talking) {
					cap_send(CAP_END, 0, NULL, 0);
				}
				break;
			}
		}
		cap.level = 0;
		if (pa) {
			pa_simple_free(pa);
		}
	}
	rnnoise_destroy(dn);
	opus_encoder_destroy(enc);
	return NULL;
}

int capture_start(int pipe_wr, const capture_opts *o) {
	cap.pipe_wr = pipe_wr;
	cap.o = *o;
	cap.vox = o->vox;
	cap.quit = false;
	if (pthread_create(&cap.thread, NULL, capture_thread, NULL) != 0) {
		return -1;
	}
	cap.running = true;
	return 0;
}

void capture_begin(void) {
	pthread_mutex_lock(&cap.mu);
	cap.want = true;
	cap.begins++;
	pthread_cond_signal(&cap.cv);
	pthread_mutex_unlock(&cap.mu);
}

void capture_end(void) {
	pthread_mutex_lock(&cap.mu);
	cap.want = false;
	pthread_cond_signal(&cap.cv);
	pthread_mutex_unlock(&cap.mu);
}

void capture_set_vox(bool on) {
	pthread_mutex_lock(&cap.mu);
	cap.vox = on;
	pthread_cond_signal(&cap.cv);
	pthread_mutex_unlock(&cap.mu);
}

bool capture_vox(void) {
	return cap.vox;
}

int capture_level(void) {
	return cap.level;
}

void capture_stop(void) {
	if (!cap.running) {
		return;
	}
	pthread_mutex_lock(&cap.mu);
	cap.quit = true;
	pthread_cond_signal(&cap.cv);
	pthread_mutex_unlock(&cap.mu);
	pthread_join(cap.thread, NULL);
	cap.running = false;
}

/* ---------- codec helpers ---------- */

OpusEncoder *ptt_encoder_create(void) {
	int err = 0;
	OpusEncoder *e = opus_encoder_create(AUDIO_RATE, 1, OPUS_APPLICATION_VOIP, &err);
	if (!e) {
		return NULL;
	}
	opus_encoder_ctl(e, OPUS_SET_MAX_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND));
	opus_encoder_ctl(e, OPUS_SET_BANDWIDTH(OPUS_AUTO));
	opus_encoder_ctl(e, OPUS_SET_VBR(1));
	opus_encoder_ctl(e, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(e, OPUS_SET_PACKET_LOSS_PERC(20));
	opus_encoder_ctl(e, OPUS_SET_COMPLEXITY(10));
	opus_encoder_ctl(e, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	/* what the app's 16 kHz encoder settles on by itself: 60*Fs/frame + Fs */
	opus_encoder_ctl(e, OPUS_SET_BITRATE(17000));
	return e;
}

int rxdec_init(rx_decoder *d) {
	int err = 0;
	memset(d, 0, sizeof *d);
	d->dec = opus_decoder_create(AUDIO_RATE, 1, &err);
	d->last_samples = FRAME_SAMPLES;
	return d->dec ? 0 : -1;
}

void rxdec_free(rx_decoder *d) {
	if (d->dec) {
		opus_decoder_destroy(d->dec);
	}
	d->dec = NULL;
}

void rxdec_lost(rx_decoder *d, int count) {
	d->lost += count;
	if (d->lost > 20) {
		d->lost = 20; /* beyond ~2 s a gap is silence, not something to conceal */
	}
}

int rxdec_decode(rx_decoder *d, const uint8_t *data, size_t len, int16_t *out, size_t cap) {
	int total = 0;
	int ps = opus_packet_get_nb_samples(data, (opus_int32)len, AUDIO_RATE);
	if (ps <= 0 || ps > MAX_PACKET_SAMPLES) {
		ps = d->last_samples;
	}
	while (d->lost > 0 && (size_t)(total + ps * 2) <= cap) {
		int r;
		if (d->lost == 1) {
			/* the lost packet's own audio rides in this one as FEC */
			r = opus_decode(d->dec, data, (opus_int32)len, out + total, ps, 1);
		} else {
			r = opus_decode(d->dec, NULL, 0, out + total, d->last_samples, 0);
		}
		if (r > 0) {
			total += r;
		}
		d->lost--;
	}
	d->lost = 0;
	if ((size_t)(total + ps) > cap) {
		return total;
	}
	int r = opus_decode(d->dec, data, (opus_int32)len, out + total, MAX_PACKET_SAMPLES < (int)(cap - (size_t)total) ? MAX_PACKET_SAMPLES : (int)(cap - (size_t)total), 0);
	if (r > 0) {
		total += r;
		d->last_samples = r;
	}
	return total;
}

/* ---------- WAV ---------- */

static uint32_t le32(const uint8_t *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t le16(const uint8_t *p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

int wav_load(const char *path, int16_t **pcm, size_t *n, char *err, size_t errn) {
	size_t len;
	uint8_t *b = read_file(path, &len, 512u << 20);
	if (!b) {
		snprintf(err, errn, "cannot read %s", path);
		return -1;
	}
	if (len < 12 || memcmp(b, "RIFF", 4) != 0 || memcmp(b + 8, "WAVE", 4) != 0) {
		snprintf(err, errn, "%s is not a WAV file", path);
		free(b);
		return -1;
	}
	int fmt = 0, ch = 0, rate = 0, bits = 0;
	const uint8_t *data = NULL;
	size_t data_n = 0;
	for (size_t off = 12; off + 8 <= len;) {
		uint32_t sz = le32(b + off + 4);
		const uint8_t *body = b + off + 8;
		if (sz > len - off - 8) {
			sz = (uint32_t)(len - off - 8);
		}
		if (memcmp(b + off, "fmt ", 4) == 0 && sz >= 16) {
			fmt = le16(body);
			ch = le16(body + 2);
			rate = (int)le32(body + 4);
			bits = le16(body + 14);
			if (fmt == 0xFFFE && sz >= 26) {
				fmt = le16(body + 24); /* WAVE_FORMAT_EXTENSIBLE: the subformat */
			}
		} else if (memcmp(b + off, "data", 4) == 0) {
			data = body;
			data_n = sz;
		}
		off += 8 + sz + (sz & 1);
	}
	if (!data || ch < 1 || rate < 4000 || !((fmt == 1 && bits == 16) || (fmt == 3 && bits == 32))) {
		snprintf(err, errn, "%s: need 16-bit PCM or 32-bit float WAV", path);
		free(b);
		return -1;
	}
	size_t frames = data_n / (size_t)(ch * bits / 8);
	float *mono = malloc((frames + 1) * sizeof *mono);
	for (size_t f = 0; f < frames; f++) {
		float acc = 0;
		for (int c = 0; c < ch; c++) {
			const uint8_t *s = data + (f * (size_t)ch + (size_t)c) * (size_t)(bits / 8);
			if (fmt == 1) {
				acc += (int16_t)le16(s);
			} else {
				float v;
				uint32_t u = le32(s);
				memcpy(&v, &u, 4);
				acc += v * 32767.0f;
			}
		}
		mono[f] = acc / (float)ch;
	}
	free(b);
	/* linear resample to 48 kHz; fine for speech */
	size_t out_n = (size_t)((double)frames * AUDIO_RATE / rate);
	int16_t *out = malloc((out_n + 1) * sizeof *out);
	for (size_t k = 0; k < out_n; k++) {
		double pos = (double)k * rate / AUDIO_RATE;
		size_t i = (size_t)pos;
		double fr = pos - (double)i;
		float a = mono[i < frames ? i : frames - 1];
		float c = mono[i + 1 < frames ? i + 1 : frames - 1];
		float v = (float)(a + (c - a) * fr);
		out[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
	free(mono);
	*pcm = out;
	*n = out_n;
	return 0;
}

int wav_save(const char *path, const int16_t *pcm, size_t n, int rate) {
	FILE *f = fopen(path, "wb");
	if (!f) {
		return -1;
	}
	uint32_t data_n = (uint32_t)(n * 2);
	uint8_t h[44];
	memcpy(h, "RIFF", 4);
	uint32_t v = 36 + data_n;
	memcpy(h + 4, &v, 4);
	memcpy(h + 8, "WAVEfmt ", 8);
	v = 16;
	memcpy(h + 16, &v, 4);
	uint16_t s = 1;
	memcpy(h + 20, &s, 2);
	memcpy(h + 22, &s, 2);
	v = (uint32_t)rate;
	memcpy(h + 24, &v, 4);
	v = (uint32_t)rate * 2;
	memcpy(h + 28, &v, 4);
	s = 2;
	memcpy(h + 32, &s, 2);
	s = 16;
	memcpy(h + 34, &s, 2);
	memcpy(h + 36, "data", 4);
	memcpy(h + 40, &data_n, 4);
	fwrite(h, 1, sizeof h, f);
	fwrite(pcm, 2, n, f);
	return fclose(f);
}
