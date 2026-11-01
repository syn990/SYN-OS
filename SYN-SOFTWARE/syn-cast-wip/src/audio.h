/* ------------------------------------------------------------------------
 *   Voice in and out. One rate everywhere: 48 kHz mono s16, 60 ms frames.
 *
 *   Out: a ring buffer drained by a playback thread into PulseAudio. The
 *   main loop decides what goes in (live streams, replays) and when.
 *   In:  a capture thread that opens the mic only while talking (or all
 *   the time in VOX mode), cleans it with RNNoise, encodes Opus with the
 *   phone app's settings and hands packets to the main loop over a pipe.
 *
 *   Encoder settings recovered from libzello.opus.so (7.19.2): VOIP
 *   application, VBR, in-band FEC, 20% expected loss, complexity 10,
 *   bandwidth auto, 60 ms frames. Its API rate is 16 kHz; this one runs
 *   at 48 kHz (RNNoise's rate) capped to wideband with the bitrate the
 *   16 kHz encoder picks by itself, so the bitstream is the same kind.
 *
 *   SYN_CAST_NOAUDIO=1 replaces both ends with clocks: playback drains in
 *   real time (into $SYN_CAST_PLAYOUT as raw s16 if set) and the "mic" is
 *   a 600 Hz tone. The tests run that way.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_AUDIO_H
#define SYN_CAST_AUDIO_H

#include "zproto.h"

#include <opus/opus.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_RATE 48000
#define FRAME_MS 60
#define FRAME_SAMPLES (AUDIO_RATE * FRAME_MS / 1000)
#define MAX_PACKET_SAMPLES (AUDIO_RATE * 120 / 1000)
#define MAX_OPUS_PACKET 1275

/* What this app announces in start_stream: 16 kHz, 1 frame, 60 ms. */
extern const zp_codec PTT_CODEC;

bool audio_null(void);

/* ---------- playback ---------- */
int playback_start(void);
void playback_stop(void);
size_t playback_write(const int16_t *pcm, size_t n);
size_t playback_queued(void);        /* in the ring, not yet handed out */
uint64_t playback_written(void);     /* totals since start, in samples */
uint64_t playback_played(void);
int playback_level(void);            /* 0..100 */
void playback_set_gain_db(double db);
void playback_flush(void);

/* ---------- capture ---------- */
enum { CAP_PACKET = 1, CAP_END, CAP_VOX_START, CAP_ERROR };
typedef struct {
	uint8_t kind;
	uint8_t level;   /* 0..100 */
	uint16_t len;
} cap_hdr;            /* followed by len bytes on the pipe */

typedef struct {
	bool noise_suppression;
	double gain_db;
	bool vox;
	double vox_threshold;
} capture_opts;

int capture_start(int pipe_wr, const capture_opts *o);
void capture_begin(void);            /* PTT down */
void capture_end(void);              /* PTT up */
void capture_set_vox(bool on);
bool capture_vox(void);
int capture_level(void);
void capture_stop(void);

/* ---------- codec helpers ---------- */
OpusEncoder *ptt_encoder_create(void);

/* Decodes a received or stored packet sequence, covering losses with the
 * next packet's FEC or with concealment. */
typedef struct {
	OpusDecoder *dec;
	int lost;                  /* lost packets not yet covered */
	int last_samples;          /* size of the last good packet */
} rx_decoder;

int rxdec_init(rx_decoder *d);
void rxdec_free(rx_decoder *d);
void rxdec_lost(rx_decoder *d, int count);
/* Writes PCM for any covered losses plus this packet; returns samples. */
int rxdec_decode(rx_decoder *d, const uint8_t *data, size_t len, int16_t *out, size_t cap);

/* ---------- WAV ---------- */
int wav_load(const char *path, int16_t **pcm, size_t *n, char *err, size_t errn);
int wav_save(const char *path, const int16_t *pcm, size_t n, int rate);

#endif
