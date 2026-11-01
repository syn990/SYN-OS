/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "history.h"
#include "util.h"

#include <jansson.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *KIND[] = {"voice", "text", "image", "location", "event"};

static void slug(const char *channel, char *out, size_t cap) {
	size_t o = 0;
	for (const char *p = channel; *p && o + 1 < cap; p++) {
		char c = *p;
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.';
		out[o++] = ok ? c : '_';
	}
	if (o == 0) {
		out[o++] = '_';
	} else if (out[0] == '.') {
		out[0] = '_'; /* never "." or ".." */
	}
	out[o] = 0;
}

void history_voice_path(const history *h, uint64_t id, char *out, size_t cap) {
	snprintf(out, cap, "%s/v%llu.spt", h->dir, (unsigned long long)id);
}

void history_image_path(const history *h, uint64_t id, bool thumb, char *out, size_t cap) {
	snprintf(out, cap, "%s/i%llu%s.jpg", h->dir, (unsigned long long)id, thumb ? ".t" : "");
}

void history_set_text(hmsg *m, const char *text) {
	free(m->text);
	m->text = text ? strdup(text) : NULL;
}

static json_t *to_json(const hmsg *m) {
	json_t *o = json_object();
	json_object_set_new(o, "id", json_integer((json_int_t)m->id));
	json_object_set_new(o, "ts", json_integer(m->ts));
	json_object_set_new(o, "kind", json_string(KIND[m->kind]));
	if (m->out) json_object_set_new(o, "out", json_true());
	if (m->played) json_object_set_new(o, "played", json_true());
	if (m->partial) json_object_set_new(o, "partial", json_true());
	json_object_set_new(o, "from", json_string(m->from));
	if (m->forwho[0]) json_object_set_new(o, "for", json_string(m->forwho));
	if (m->duration_ms) json_object_set_new(o, "duration_ms", json_integer(m->duration_ms));
	if (m->w) json_object_set_new(o, "w", json_integer(m->w));
	if (m->h) json_object_set_new(o, "h", json_integer(m->h));
	if (m->kind == HM_LOCATION) {
		json_object_set_new(o, "lat", json_real(m->lat));
		json_object_set_new(o, "lon", json_real(m->lon));
		json_object_set_new(o, "acc", json_real(m->acc));
	}
	if (m->text) json_object_set_new(o, "text", json_string(m->text));
	return o;
}

static int from_json(json_t *o, hmsg *m) {
	memset(m, 0, sizeof *m);
	m->id = (uint64_t)json_integer_value(json_object_get(o, "id"));
	if (!m->id) {
		return -1;
	}
	m->ts = json_integer_value(json_object_get(o, "ts"));
	const char *kind = json_string_value(json_object_get(o, "kind"));
	m->kind = HM_EVENT;
	for (int k = 0; k < 5; k++) {
		if (kind && strcmp(kind, KIND[k]) == 0) {
			m->kind = (hm_kind)k;
		}
	}
	m->out = json_is_true(json_object_get(o, "out"));
	m->played = json_is_true(json_object_get(o, "played"));
	m->partial = json_is_true(json_object_get(o, "partial"));
	const char *s;
	if ((s = json_string_value(json_object_get(o, "from")))) str_copy(m->from, sizeof m->from, s);
	if ((s = json_string_value(json_object_get(o, "for")))) str_copy(m->forwho, sizeof m->forwho, s);
	m->duration_ms = (int)json_integer_value(json_object_get(o, "duration_ms"));
	m->w = (int)json_integer_value(json_object_get(o, "w"));
	m->h = (int)json_integer_value(json_object_get(o, "h"));
	m->lat = json_number_value(json_object_get(o, "lat"));
	m->lon = json_number_value(json_object_get(o, "lon"));
	m->acc = json_number_value(json_object_get(o, "acc"));
	if ((s = json_string_value(json_object_get(o, "text")))) m->text = strdup(s);
	return 0;
}

static void append_line(history *h, const hmsg *m) {
	if (!h->index) {
		return;
	}
	json_t *o = to_json(m);
	char *line = json_dumps(o, JSON_COMPACT);
	json_decref(o);
	if (line) {
		fprintf(h->index, "%s\n", line);
		fflush(h->index);
		free(line);
	}
}

hmsg *history_find(history *h, uint64_t id) {
	for (size_t k = h->n; k-- > 0;) {
		if (h->v[k].id == id) {
			return &h->v[k];
		}
	}
	return NULL;
}

static hmsg *push(history *h, const hmsg *m) {
	if (h->n == h->cap) {
		size_t nc = h->cap ? h->cap * 2 : 256;
		hmsg *nv = realloc(h->v, nc * sizeof *nv);
		if (!nv) {
			return NULL;
		}
		h->v = nv;
		h->cap = nc;
	}
	h->v[h->n] = *m;
	return &h->v[h->n++];
}

static void rewrite(history *h) {
	char path[1100], tmp[1100];
	snprintf(path, sizeof path, "%s/index.jsonl", h->dir);
	snprintf(tmp, sizeof tmp, "%s/index.jsonl.tmp", h->dir);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return;
	}
	for (size_t k = 0; k < h->n; k++) {
		json_t *o = to_json(&h->v[k]);
		char *line = json_dumps(o, JSON_COMPACT);
		json_decref(o);
		fprintf(f, "%s\n", line);
		free(line);
	}
	if (fclose(f) == 0) {
		rename(tmp, path);
	}
	if (h->index) {
		fclose(h->index);
	}
	h->index = fopen(path, "a");
}

int history_open(history *h, const char *base_dir, const char *channel) {
	memset(h, 0, sizeof *h);
	char s[256];
	slug(channel, s, sizeof s);
	snprintf(h->dir, sizeof h->dir, "%s/%s", base_dir, s);
	if (mkdir_p(h->dir, 0700) != 0) {
		return -1;
	}
	char path[1100];
	snprintf(path, sizeof path, "%s/index.jsonl", h->dir);
	FILE *f = fopen(path, "r");
	size_t lines = 0;
	if (f) {
		char *line = NULL;
		size_t cap = 0;
		while (getline(&line, &cap, f) > 0) {
			lines++;
			json_error_t jerr;
			json_t *o = json_loads(line, 0, &jerr);
			hmsg m;
			if (o && from_json(o, &m) == 0) {
				hmsg *old = history_find(h, m.id);
				if (old) {
					free(old->text);
					*old = m;
				} else {
					push(h, &m);
				}
			}
			json_decref(o);
		}
		free(line);
		fclose(f);
	}
	/* a message cut off by a crash is as complete as it will get */
	for (size_t k = 0; k < h->n; k++) {
		h->v[k].partial = false;
	}
	if (lines > h->n + h->n / 2 + 64) {
		rewrite(h);
	} else {
		h->index = fopen(path, "a");
	}
	return h->index ? 0 : -1;
}

void history_close(history *h) {
	if (h->index) {
		fclose(h->index);
	}
	for (size_t k = 0; k < h->n; k++) {
		free(h->v[k].text);
	}
	free(h->v);
	memset(h, 0, sizeof *h);
}

uint64_t history_add(history *h, const hmsg *m) {
	static uint64_t last;
	hmsg c = *m;
	if (!c.ts) {
		c.ts = wall_ms();
	}
	if (!c.id) {
		c.id = (uint64_t)wall_ms() * 1000;
		if (c.id <= last) {
			c.id = last + 1;
		}
	}
	last = c.id > last ? c.id : last;
	c.text = m->text ? strdup(m->text) : NULL;
	hmsg *p = push(h, &c);
	if (!p) {
		free(c.text);
		return 0;
	}
	append_line(h, p);
	return p->id;
}

void history_commit(history *h, const hmsg *m) {
	append_line(h, m);
}

static long long file_size(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 ? (long long)st.st_size : 0;
}

static long long msg_bytes(history *h, const hmsg *m) {
	char p[1100];
	if (m->kind == HM_VOICE) {
		history_voice_path(h, m->id, p, sizeof p);
		return file_size(p);
	}
	if (m->kind == HM_IMAGE) {
		history_image_path(h, m->id, false, p, sizeof p);
		long long n = file_size(p);
		history_image_path(h, m->id, true, p, sizeof p);
		return n + file_size(p);
	}
	return 0;
}

static void msg_unlink(history *h, const hmsg *m) {
	char p[1100];
	if (m->kind == HM_VOICE) {
		history_voice_path(h, m->id, p, sizeof p);
		unlink(p);
	} else if (m->kind == HM_IMAGE) {
		history_image_path(h, m->id, false, p, sizeof p);
		unlink(p);
		history_image_path(h, m->id, true, p, sizeof p);
		unlink(p);
	}
}

int history_prune(history *h, int days, long long max_bytes) {
	int64_t cutoff = days > 0 ? wall_ms() - (int64_t)days * 86400000 : INT64_MIN;
	long long total = 0;
	for (size_t k = 0; k < h->n; k++) {
		total += msg_bytes(h, &h->v[k]);
	}
	/* the index is in arrival order, so the size limit drops from the front;
	 * the age limit checks every message (a clock change can reorder) */
	size_t kept = 0, dropped = 0;
	for (size_t k = 0; k < h->n; k++) {
		hmsg *m = &h->v[k];
		bool over = max_bytes > 0 && total > max_bytes;
		if (m->ts < cutoff || over) {
			total -= msg_bytes(h, m);
			msg_unlink(h, m);
			free(m->text);
			dropped++;
		} else {
			h->v[kept++] = *m;
		}
	}
	if (dropped) {
		h->n = kept;
		rewrite(h);
	}
	return (int)dropped;
}

/* ---------- voice files ---------- */

int hv_open(hv_writer *w, const char *path, const zp_codec *c) {
	memset(w, 0, sizeof *w);
	w->f = fopen(path, "wb");
	if (!w->f) {
		return -1;
	}
	uint8_t hdr[8] = {'S', 'P', 'T', '1', (uint8_t)(c->rate & 0xFF), (uint8_t)(c->rate >> 8),
		c->frames_per_packet, c->frame_ms};
	fwrite(hdr, 1, sizeof hdr, w->f);
	w->packet_ms = c->frame_ms * c->frames_per_packet;
	return 0;
}

int hv_write(hv_writer *w, const uint8_t *data, size_t n) {
	if (!w->f || n > 65535) {
		return -1;
	}
	uint8_t len[2] = {(uint8_t)(n >> 8), (uint8_t)n};
	fwrite(len, 1, 2, w->f);
	if (n) {
		fwrite(data, 1, n, w->f);
	}
	w->packets++;
	return 0;
}

int hv_duration_ms(const hv_writer *w) {
	return w->packets * w->packet_ms;
}

void hv_close(hv_writer *w) {
	if (w->f) {
		fclose(w->f);
	}
	w->f = NULL;
}

int hv_load(const char *path, hv_file *out) {
	memset(out, 0, sizeof *out);
	size_t n;
	uint8_t *b = read_file(path, &n, 64u << 20);
	if (!b) {
		return -1;
	}
	if (n < 8 || memcmp(b, "SPT1", 4) != 0) {
		free(b);
		return -1;
	}
	out->codec.rate = (uint16_t)(b[4] | (b[5] << 8));
	out->codec.frames_per_packet = b[6];
	out->codec.frame_ms = b[7];
	size_t cap = 0;
	for (size_t off = 8; off + 2 <= n;) {
		size_t len = ((size_t)b[off] << 8) | b[off + 1];
		off += 2;
		if (off + len > n) {
			break; /* a torn final packet */
		}
		if (out->n == cap) {
			cap = cap ? cap * 2 : 256;
			hv_packet *np = realloc(out->p, cap * sizeof *np);
			if (!np) {
				break;
			}
			out->p = np;
		}
		out->p[out->n].len = (uint16_t)len;
		out->p[out->n].data = b + off;
		out->n++;
		off += len;
	}
	out->blob = b;
	return 0;
}

void hv_free(hv_file *f) {
	free(f->p);
	free(f->blob);
	memset(f, 0, sizeof *f);
}
