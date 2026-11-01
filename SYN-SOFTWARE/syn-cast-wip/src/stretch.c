/* ------------------------------------------------------------------------
 *   WSOLA: cut the input into 30 ms Hann windows, place them every 15 ms
 *   in the output, but take each one from about 15*speed ms further along
 *   the input, nudged up to 10 ms either way to the spot that best
 *   continues the previous window. Pitch stays, pace changes.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "stretch.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define WIN 1440           /* 30 ms at 48 kHz */
#define HOP (WIN / 2)      /* synthesis hop */
#define DELTA 480          /* search +-10 ms */

struct stretch {
	double speed;
	float win[WIN];
	float *in;             /* in[0] is absolute sample in_base */
	size_t in_len, in_cap, in_base;
	double ana;            /* nominal next analysis position (absolute) */
	size_t prev;           /* start of the previous chosen window */
	bool have_prev;
	float ola[WIN];
	int16_t *out;          /* produced, not yet handed back */
	size_t out_len, out_cap;
	size_t real_end;       /* absolute end of real input once flushing */
};

stretch *stretch_new(void) {
	stretch *s = calloc(1, sizeof *s);
	for (int i = 0; i < WIN; i++) {
		s->win[i] = (float)(0.5 - 0.5 * cos(2 * M_PI * i / WIN));
	}
	stretch_reset(s, 1.0);
	return s;
}

void stretch_free(stretch *s) {
	if (s) {
		free(s->in);
		free(s->out);
		free(s);
	}
}

void stretch_reset(stretch *s, double speed) {
	s->speed = speed < 0.5 ? 0.5 : speed > 2.0 ? 2.0 : speed;
	s->in_len = 0;
	s->in_base = 0;
	s->ana = 0;
	s->prev = 0;
	s->have_prev = false;
	s->out_len = 0;
	s->real_end = 0;
	memset(s->ola, 0, sizeof s->ola);
}

static void out_push(stretch *s, const float *v, size_t n) {
	if (s->out_len + n > s->out_cap) {
		size_t nc = s->out_cap ? s->out_cap * 2 : 8192;
		while (nc < s->out_len + n) nc *= 2;
		s->out = realloc(s->out, nc * sizeof *s->out);
		s->out_cap = nc;
	}
	for (size_t i = 0; i < n; i++) {
		float x = v[i];
		s->out[s->out_len++] = (int16_t)(x > 32767 ? 32767 : x < -32768 ? -32768 : x);
	}
}

static size_t out_take(stretch *s, int16_t *out, size_t cap) {
	size_t n = s->out_len < cap ? s->out_len : cap;
	memcpy(out, s->out, n * sizeof *out);
	memmove(s->out, s->out + n, (s->out_len - n) * sizeof *s->out);
	s->out_len -= n;
	return n;
}

static void in_push(stretch *s, const int16_t *in, size_t n, bool zeros) {
	if (s->in_len + n > s->in_cap) {
		size_t nc = s->in_cap ? s->in_cap * 2 : 16384;
		while (nc < s->in_len + n) nc *= 2;
		s->in = realloc(s->in, nc * sizeof *s->in);
		s->in_cap = nc;
	}
	for (size_t i = 0; i < n; i++) {
		s->in[s->in_len + i] = zeros ? 0.0f : in[i];
	}
	s->in_len += n;
}

static float *at(stretch *s, size_t abs) {
	return s->in + (abs - s->in_base);
}

/* One window, if the input reaches far enough. */
static bool step(stretch *s) {
	size_t nominal = (size_t)llround(s->ana);
	size_t lo = nominal > DELTA ? nominal - DELTA : 0;
	if (lo < s->in_base) {
		lo = s->in_base;
	}
	size_t hi = nominal + DELTA;
	size_t end = s->in_base + s->in_len;
	if (hi + WIN > end || (s->have_prev && s->prev + HOP + WIN > end)) {
		return false;
	}
	size_t best = nominal < lo ? lo : nominal;
	if (s->have_prev) {
		const float *nat = at(s, s->prev + HOP);
		double best_score = -1e30;
		for (size_t c = lo; c <= hi; c += 2) {
			const float *cand = at(s, c);
			double dot = 0, energy = 1e-9;
			for (int i = 0; i < HOP; i += 4) {
				dot += (double)cand[i] * nat[i];
				energy += (double)cand[i] * cand[i];
			}
			double score = dot / sqrt(energy);
			if (score > best_score) {
				best_score = score;
				best = c;
			}
		}
	}
	const float *x = at(s, best);
	for (int i = 0; i < WIN; i++) {
		s->ola[i] += s->win[i] * x[i];
	}
	out_push(s, s->ola, HOP);
	memmove(s->ola, s->ola + HOP, (WIN - HOP) * sizeof *s->ola);
	memset(s->ola + (WIN - HOP), 0, HOP * sizeof *s->ola);
	s->prev = best;
	s->have_prev = true;
	s->ana += HOP * s->speed;

	/* drop input nothing will look at again */
	size_t keep_from = (size_t)s->ana > DELTA ? (size_t)s->ana - DELTA : 0;
	if (s->prev + HOP < keep_from) {
		keep_from = s->prev + HOP;
	}
	if (keep_from > s->in_base + 4096) {
		size_t drop = keep_from - s->in_base;
		memmove(s->in, s->in + drop, (s->in_len - drop) * sizeof *s->in);
		s->in_len -= drop;
		s->in_base += drop;
	}
	return true;
}

size_t stretch_process(stretch *s, const int16_t *in, size_t n, int16_t *out, size_t cap) {
	if (s->speed == 1.0) {
		size_t k = n < cap ? n : cap;
		memcpy(out, in, k * sizeof *out);
		return k;
	}
	in_push(s, in, n, false);
	while (step(s)) {
	}
	return out_take(s, out, cap);
}

size_t stretch_flush(stretch *s, int16_t *out, size_t cap) {
	if (s->speed == 1.0) {
		return 0;
	}
	if (!s->real_end) {
		s->real_end = s->in_base + s->in_len;
		in_push(s, NULL, WIN + 2 * DELTA + HOP, true);
		while (s->ana < (double)s->real_end && step(s)) {
		}
		out_push(s, s->ola, HOP);
		memset(s->ola, 0, sizeof s->ola);
	}
	return out_take(s, out, cap);
}
