/* ------------------------------------------------------------------------
 *   Per-channel message history on disk, the way the phone app keeps it:
 *   every voice message replayable, texts, images and locations, with
 *   age and size limits.
 *
 *   <data>/<channel>/index.jsonl   one JSON record per line; a later record
 *                                  with the same id replaces the earlier
 *   <data>/<channel>/v<id>.spt     a voice message: "SPT1", the codec header
 *                                  (rate u16 LE, frames/packet, frame ms),
 *                                  then [len u16 BE][opus packet]...; a
 *                                  zero length marks a lost packet
 *   <data>/<channel>/i<id>.jpg, i<id>.t.jpg   image and thumbnail
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_HISTORY_H
#define SYN_CAST_HISTORY_H

#include "zproto.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef enum { HM_VOICE, HM_TEXT, HM_IMAGE, HM_LOCATION, HM_EVENT } hm_kind;

typedef struct {
	uint64_t id;
	int64_t ts;          /* unix ms */
	hm_kind kind;
	bool out;            /* sent from here */
	bool played;         /* voice: heard at least once */
	bool partial;        /* voice still arriving, image still loading */
	char from[64];
	char forwho[64];     /* private recipient, "" for the channel */
	/* G228: a line a cast member spoke for a character: its id, display
	 * name and #rrggbb colour ("" when the member spoke as themselves) */
	char as[64];
	char name[64];
	char colour[16];
	int duration_ms;
	int w, h;
	double lat, lon, acc;
	char *text;          /* text, address, transcription, or event line */
} hmsg;

typedef struct {
	char dir[1024];
	hmsg *v;
	size_t n, cap;
	FILE *index;
} history;

int history_open(history *h, const char *base_dir, const char *channel);
void history_close(history *h);

/* Appends m (assigning id and ts when zero) and returns its id. */
uint64_t history_add(history *h, const hmsg *m);
hmsg *history_find(history *h, uint64_t id);
/* Rewrites the record after its fields changed. */
void history_commit(history *h, const hmsg *m);
void history_set_text(hmsg *m, const char *text);
/* The name a line is shown under: the character's name, else its id, else
 * the member who sent it. */
const char *hmsg_who(const hmsg *m);
/* Copies who spoke (as, name, colour) from a parsed event or history line. */
void hmsg_speaker(hmsg *m, const char *as, const char *name, const char *colour);

void history_voice_path(const history *h, uint64_t id, char *out, size_t cap);
void history_image_path(const history *h, uint64_t id, bool thumb, char *out, size_t cap);

/* Drops messages older than days and, oldest first, until the files fit
 * max_bytes. Returns how many were removed. */
int history_prune(history *h, int days, long long max_bytes);

/* ---------- voice files ---------- */

typedef struct {
	FILE *f;
	int packets;
	int packet_ms;
} hv_writer;

int hv_open(hv_writer *w, const char *path, const zp_codec *c);
int hv_write(hv_writer *w, const uint8_t *data, size_t n);  /* n==0: lost */
int hv_duration_ms(const hv_writer *w);
void hv_close(hv_writer *w);

typedef struct {
	uint16_t len;
	uint8_t *data;
} hv_packet;

typedef struct {
	zp_codec codec;
	hv_packet *p;
	size_t n;
	uint8_t *blob;
} hv_file;

int hv_load(const char *path, hv_file *out);
void hv_free(hv_file *f);

#endif
