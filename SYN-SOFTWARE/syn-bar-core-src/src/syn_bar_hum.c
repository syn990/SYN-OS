/* ------------------------------------------------------------------------
 *   The acoustic monitor's voice. Every style is built the same way:
 *   short sounds and slow, breathing pads, in a room of its own, under a
 *   ceiling no load can push it through. A busy machine makes the short
 *   sounds come more often; nothing turns into a tone that stays on and
 *   gets louder with the load, because that's what shakes the house.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-CORE (acoustic monitor)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#define _GNU_SOURCE
#include "syn_bar_hum.h"
#include "syn_hum_styles.h"

#include <pulse/error.h>
#include <pulse/simple.h>

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define RATE 48000
#define TICK_FRAMES (RATE / 10)  /* one HUM line's worth: a tenth of a second */
#define BLOCK_FRAMES (RATE / 50) /* the voice plays 20 ms at a time, so a one-off starts within that */
#define BLOCKS_PER_TICK (TICK_FRAMES / BLOCK_FRAMES)
#define TAU (2.0 * M_PI)
#define F0 110.0                 /* the fundamental: A2 */
#define MAX_VOICES 16            /* cores past the 16th have no note of their own yet */

#define LINE_RATE 8000
#define UPSAMPLE (RATE / LINE_RATE)
#define FRAME 160                /* 20 ms: one G.711 packet */
#define CNG_DB -68.0             /* comfort noise: there on headphones, gone on speakers */

#define HOLD_STEP 11             /* from here up something can hold instead of happening now and then */
#define HOLD_TICKS 30            /* and does once it's stayed there 3 s: a spike isn't a runaway */
#define QUIET_SECONDS 2.0        /* no HUM line for this long and the voice fades out */
#define ECHO_MAX (RATE * 6 / 10) /* the longest echo, 0.6 s */
#define WOB 2048                 /* the tape wobble: up to 40 ms of delay line */
#define CEILING_DB -22.0         /* over this, everything is pressed down 4:1, at any load and volume */
#define LIMIT_DB -14.0           /* and no single click or bell's attack gets past this */
#define SIXTEENTH (RATE * 60 / 128 / 4) /* techno's grid: a sixteenth at 128 bpm, 5625 samples */
#define GRAINS 24                /* techno's dust and sparks, at once */

/* ---- the steps: one HUM line ----------------------------------------- */

typedef struct {
	int cores[MAX_VOICES];
	int core_count;
	int stall, fsk, floor; /* memory, network, disk */
	int error, inbound;    /* one-off figures */
	int spawn, sched;      /* processes started, context switches */
	int cpu_wait, io_wait; /* tasks waiting for a CPU, on IO */
} hum_steps;

/* ---- randomness and levels ------------------------------------------- */

static double frand(uint32_t *s) {
	*s ^= *s << 13;
	*s ^= *s >> 17;
	*s ^= *s << 5;
	return (double)*s / 2147483648.0 - 1.0;
}

static double chance(uint32_t *s) {
	return (frand(s) + 1.0) / 2.0;
}

static double db(double v) {
	return pow(10.0, v / 20.0);
}

/* An everyday sound's level for a step: -28 dB at step 15, 0.7 dB less a
 * step under it, so a lightly loaded core is still heard */
static double step_db(int step) {
	return -28.0 - 0.7 * (15 - (step < 1 ? 1 : step));
}

static void wrap(double *phase) {
	if (*phase >= TAU) *phase -= TAU;
}

/* ---- what's been heard, for the bar's tooltip ------------------------ */

#define HEARD_MAX 8
static char heard_when[HEARD_MAX][16], heard_what[HEARD_MAX][80];
static int heard_times[HEARD_MAX]; /* the same sound again folds into its line: "(x3)" */
static int heard_next, heard_count;
static pthread_mutex_t heard_lock = PTHREAD_MUTEX_INITIALIZER;

static void heard_add(const char *what) {
	char when[16];
	time_t now = time(NULL);
	struct tm tm_buf;
	localtime_r(&now, &tm_buf);
	strftime(when, sizeof(when), "%H:%M:%S", &tm_buf);
	pthread_mutex_lock(&heard_lock);
	int last = (heard_next - 1 + HEARD_MAX) % HEARD_MAX;
	if (heard_count && !strcmp(heard_what[last], what)) {
		heard_times[last]++;
		memcpy(heard_when[last], when, sizeof(when));
	} else {
		memcpy(heard_when[heard_next], when, sizeof(when));
		snprintf(heard_what[heard_next], sizeof(heard_what[0]), "%s", what);
		heard_times[heard_next] = 1;
		heard_next = (heard_next + 1) % HEARD_MAX;
		if (heard_count < HEARD_MAX) heard_count++;
	}
	pthread_mutex_unlock(&heard_lock);
}

void syn_bar_hum_heard(int fd) {
	char reply[HEARD_MAX * 120];
	size_t len = 0;
	pthread_mutex_lock(&heard_lock);
	for (int i = 0; i < heard_count; i++) {
		int at = (heard_next - 1 - i + HEARD_MAX) % HEARD_MAX;
		char times[16] = "";
		if (heard_times[at] > 1) snprintf(times, sizeof(times), " (x%d)", heard_times[at]);
		int n = snprintf(reply + len, sizeof(reply) - len, "%s %s%s\n", heard_when[at], heard_what[at], times);
		if (n < 0 || (size_t)n >= sizeof(reply) - len) break;
		len += (size_t)n;
	}
	pthread_mutex_unlock(&heard_lock);
	if (len) write(fd, reply, len);
}

/* ---- the phreak style's line: a digital phone call ------------------- */

/* G.711 mu-law, the 8-bit code a digital call travels in */
static uint8_t ulaw_encode(int pcm) {
	int sign = 0;
	if (pcm < 0) {
		pcm = -pcm;
		sign = 0x80;
	}
	if (pcm > 32635) pcm = 32635;
	pcm += 0x84;
	int exponent = 7;
	for (int mask = 0x4000; !(pcm & mask) && exponent > 0; mask >>= 1) exponent--;
	int mantissa = (pcm >> (exponent + 3)) & 0x0F;
	return (uint8_t)~(sign | (exponent << 4) | mantissa);
}

static int ulaw_decode(uint8_t u) {
	u = (uint8_t)~u;
	int exponent = (u >> 4) & 7, mantissa = u & 0x0F;
	int v = (((mantissa << 3) + 0x84) << exponent) - 0x84;
	return (u & 0x80) ? -v : v;
}

/* One direction of a call. What goes in is encoded into a packet; each
 * finished packet meets the network, and what comes out plays one packet
 * later. */
typedef struct {
	int16_t sent[FRAME], play[FRAME], good[FRAME];
	int fill;
	int lost_run, burst;
	double cng_amp, cng_pole, cng_norm, cng_lp;
	int sid_left;
	double badness; /* 0 a clean call, 1 a bad one */
} phone_line;

static void line_init(phone_line *ln, int sid_left) {
	memset(ln, 0, sizeof(*ln));
	ln->cng_amp = db(CNG_DB);
	ln->cng_pole = 0.5;
	ln->cng_norm = 1.0;
	ln->sid_left = sid_left;
}

/* A packet meets the network: it arrives, or is lost and patched, or is
 * late, or the jitter buffer stutters it. A clean call hardly ever
 * glitches; under memory pressure it does more. */
static void line_deliver(phone_line *ln, uint32_t *rng) {
	double bad = ln->badness;
	int lost = 0;
	if (ln->burst > 0) {
		ln->burst--;
		lost = 1;
	} else if (chance(rng) < 0.0005 + 0.02 * bad) {
		ln->burst = 2 + (int)(chance(rng) * 6.0); /* several in a row */
		lost = 1;
	} else if (chance(rng) < 0.003 + 0.12 * bad) {
		lost = 1;
	}

	if (lost) {
		/* Concealment: the last packet that arrived, again, quieter each
		 * time and gone after four */
		double gain = 1.0 - 0.25 * ++ln->lost_run;
		if (gain < 0.0) gain = 0.0;
		for (int i = 0; i < FRAME; i++) ln->play[i] = (int16_t)(ln->good[i] * gain);
	} else if (chance(rng) < 0.002 + 0.05 * bad) {
		/* Too late for the jitter buffer: digital silence where it was */
		ln->lost_run = 0;
		memset(ln->play, 0, sizeof(ln->play));
		memcpy(ln->good, ln->sent, sizeof(ln->good));
	} else {
		ln->lost_run = 0;
		memcpy(ln->good, ln->sent, sizeof(ln->good));
		memcpy(ln->play, ln->sent, sizeof(ln->play));
		if (chance(rng) < 0.002 + 0.05 * bad) {
			/* The jitter buffer catching up: 5 ms played twice */
			int at = 40 + (int)(chance(rng) * 80.0);
			memcpy(ln->play + at, ln->play + at - 40, 40 * sizeof(int16_t));
		}
	}

	/* A SID packet now and then: the comfort noise steps to a new level
	 * and colour, the hiss of a call where nobody's talking */
	if (--ln->sid_left <= 0) {
		ln->sid_left = 8 + (int)(chance(rng) * 18.0);
		ln->cng_amp = db(CNG_DB + 3.0 * frand(rng));
		ln->cng_pole = 1.0 - exp(-TAU * (900.0 + 1800.0 * chance(rng)) / LINE_RATE);
		ln->cng_norm = 1.0 / sqrt(ln->cng_pole / (2.0 - ln->cng_pole) / 3.0);
	}
}

/* One 8 kHz sample in, one out */
static double line_step(phone_line *ln, double in, uint32_t *rng) {
	int i = ln->fill;
	double out = ln->play[i] / 32768.0;
	ln->cng_lp += ln->cng_pole * (frand(rng) - ln->cng_lp);
	double x = in + ln->cng_lp * ln->cng_norm * ln->cng_amp;
	ln->sent[i] = (int16_t)ulaw_decode(ulaw_encode((int)lrint(x * 32767.0)));
	if (++ln->fill == FRAME) {
		line_deliver(ln, rng);
		ln->fill = 0;
	}
	return out;
}

/* ---- events: everything that sounds is one ---------------------------- */

enum {
	SEG_SILENCE, SEG_TONE, SEG_SWEEP, SEG_FSK, SEG_CLICK, SEG_NOISE, SEG_MURMUR,
	SEG_BELL, SEG_PAD, SEG_PLUCK, SEG_WHOOSH, SEG_CREAK, SEG_KICK, SEG_FM, SEG_BYTE,
};

typedef struct {
	int kind;
	double f1, f2;  /* Hz. Tone: one or two at once. Sweep, whoosh, kick: from, to. FSK: mark, space. Noise: the band. FM: the note, and the bend's ratio to it */
	double f3, f4;  /* a pad's third and fourth notes; f3 is a kick's click, FM's bend */
	double cutoff;  /* Hz: a pad's or bytebeat's low-pass, where a pluck's starts */
	double seconds, amp;
	double fade;    /* seconds in and out; 0 is 5 ms */
	double decay;   /* seconds; 0 holds, otherwise falls away from the start */
	double flip;    /* seconds between phase reversals, a modem's answer tone */
	double baud;    /* FSK's bit rate; a pad's or noise's throb, in Hz */
	int formula, a; /* bytebeat: which formula, and its number */
} segment;

#define MAX_SEGMENTS 96
#define MAX_EVENTS 24

/* Event types, so one that mustn't overlap itself can be refused */
enum { EV_PING, EV_READOUT, EV_NET, EV_DISK, EV_ERROR, EV_INBOUND, EV_LINK, EV_MEMORY, EV_RUMBLE, EV_ATMOS = 10, EV_HOLD = 20 };

typedef struct {
	int active, type;
	segment seg[MAX_SEGMENTS];
	int count, at, pos, len;
	double pan_l, pan_r;
	double p1, p2, fsk_hz, flip_sign;
	int bit_left, flip_left;
	double c_hp, c_lp, hp, lp1, lp2, norm;
	double murmur_env, murmur_target;
	int syllable_left;
	double ph[8]; /* a bell's partials, a pad's saws */
	double flutter, flutter_target; /* a creak catching and slipping */
	int flutter_left;
	uint32_t t;                     /* bytebeat's t: the voice's clock at the 8 kHz bus */
	int crush_hold, crush_mask;     /* bytebeat under memory pressure: t held, low bits gone */
	double dc;
} event;

/* Each style's room: Freeverb's feedback, damping and level, an echo
 * (none at 0 seconds), and a low-pass before it all (none at 0 Hz). In
 * the order of SYN_HUM_UPLINK... */
static const struct {
	float feedback, damping, wet;
	double echo_seconds, echo_feedback, echo_mix;
	double lowpass;
} ROOMS[SYN_HUM_STYLE_COUNT] = {
	{0.84f, 0.25f, 1.0f, 0.45, 0.45, 0.5, 0.0},   /* uplink: a long dark room and a dotted-eighth echo */
	{0.76f, 0.45f, 0.6f, 0.0, 0.0, 0.0, 0.0},     /* phreak: a phone heard in a small room */
	{0.78f, 0.50f, 0.7f, 0.18, 0.25, 0.35, 0.0},  /* hacker: tight and dark, a short slapback */
	{0.88f, 0.20f, 1.1f, 0.6, 0.35, 0.3, 0.0},    /* harmonic: a hall, a slow echo */
	{0.86f, 0.60f, 0.9f, 0.55, 0.3, 0.3, 1600.0}, /* deep: a steel hull, dark, nothing over 1.6 kHz */
	{0.86f, 0.55f, 0.85f, 0.3515625, 0.45, 0.35, 0.0}, /* techno: a dark warehouse, an echo a dotted eighth long */
	{0.74f, 0.50f, 0.45f, 0.0, 0.0, 0.0, 0.0},    /* bytebeat: nearly dry, a small room */
};

/* A grain: a ring a few milliseconds long, bent or not */
typedef struct {
	int left;
	double ph, inc, ph2, inc2, index, index_fall, env, fall, amp, pan_l, pan_r;
} grain;

typedef struct {
	int style;
	hum_steps steps;
	double volume;
	uint32_t rng;
	int tour; /* the tour scripts its own readouts and atmosphere, and isn't "heard" */
	int up;
	double held[2], out[2];
	event events[MAX_EVENTS];
	int readout_left;                       /* ticks to the next readout */
	int hot[MAX_VOICES], hot_fsk, hot_floor; /* ticks each has been up at its holding step */
	int net_up, net_quiet;                  /* uplink: connected, and ticks since traffic dropped */
	int memory_left;                        /* ticks to memory pressure's next sound */
	uint64_t clock;                         /* samples played: techno's grid, bytebeat's t */
	double sweep;                           /* techno: the DJ's low-pass, Hz */
	double echo_fb;                         /* the echo's feedback: the room's, or techno's hanging on IO */
	grain grains[GRAINS];                   /* techno: dust and sparks */
	double dust_rate, spark_rate;           /* grains a second */
	int hot_wait, spawn_was, io_said;       /* techno: ticks tasks have waited for a CPU; what's been said */
	int crush_hold, crush_mask;             /* bytebeat: what memory pressure leaves of t and the bytes */
	/* phreak: the two calls, left and right */
	phone_line line[2];
	/* hacker: the air; deep: the hull */
	double air_lp, air_breath;
	double hull_lp, hull_breath;
	double dark[4]; /* the style's low-pass, two poles a side */
	/* the room: a tape wobble, an echo, and Freeverb's eight combs and
	 * four allpasses a side */
	int room_quiet; /* samples since anything went into it */
	double wobble, wob_phase;
	float wob_l[WOB], wob_r[WOB];
	int wob_at;
	float echo_l[ECHO_MAX], echo_r[ECHO_MAX];
	int echo_at, echo_len;
	double echo_dark_l, echo_dark_r;
	float comb[2][8][1800], comb_store[2][8];
	int comb_len[2][8], comb_pos[2][8];
	float allpass[2][4][640];
	int allpass_len[2][4], allpass_pos[2][4];
	/* the ceiling, and the limit over it */
	double ceiling_env, limit_env;
} hum_synth;

static void say(hum_synth *s, const char *what) {
	if (!s->tour) heard_add(what);
}

static double core_pan(const hum_synth *s, int core) {
	int n = s->steps.core_count;
	return n > 1 ? -0.6 + 1.2 * core / (n - 1) : 0.0;
}

/* techno: n sixteenths, in seconds a segment's length comes back from
 * exactly, so a bar of them doesn't drift off the grid */
static double sixteenths(int n) {
	return ((double)n * SIXTEENTH + 0.5) / RATE;
}

/* techno: the next line of the grid, every `n` sixteenths, counted in
 * sixteenths from the voice's start */
static uint64_t grid_next(const hum_synth *s, int n) {
	uint64_t every = (uint64_t)SIXTEENTH * n;
	return (s->clock + every - 1) / every * n;
}

/* A free slot, or NULL; an event that mustn't overlap itself gets NULL
 * while one of its type is still playing. In techno, like a bang in
 * ORCA, it waits for the grid: the next sixteenth, or for the groove
 * and the rumble the next bar. */
static event *event_new(hum_synth *s, int type, int alone, double pan) {
	event *slot = NULL;
	for (int i = 0; i < MAX_EVENTS; i++) {
		event *e = &s->events[i];
		if (e->active && e->type == type && alone) return NULL;
		if (!e->active && !slot) slot = e;
	}
	if (!slot) return NULL;
	memset(slot, 0, sizeof(*slot));
	slot->active = 1;
	slot->type = type;
	slot->pan_l = cos((pan + 1.0) * M_PI / 4.0);
	slot->pan_r = sin((pan + 1.0) * M_PI / 4.0);
	slot->flip_sign = 1.0;
	slot->t = (uint32_t)(s->clock / UPSAMPLE);
	slot->crush_hold = s->crush_hold;
	slot->crush_mask = s->crush_mask;
	if (s->style == SYN_HUM_TECHNO) {
		uint64_t gap = grid_next(s, type >= EV_HOLD || type == EV_RUMBLE ? 16 : 1) * SIXTEENTH - s->clock;
		if (gap > 0) slot->seg[slot->count++] = (segment){.kind = SEG_SILENCE, .seconds = (gap + 0.5) / RATE};
	}
	return slot;
}

static int event_playing(const hum_synth *s, int type) {
	for (int i = 0; i < MAX_EVENTS; i++) {
		if (s->events[i].active && s->events[i].type == type) return 1;
	}
	return 0;
}

static void seg_add(event *e, segment g) {
	if (e->count < MAX_SEGMENTS) e->seg[e->count++] = g;
}

static void seg_gap(event *e, double seconds) {
	seg_add(e, (segment){.kind = SEG_SILENCE, .seconds = seconds});
}

static void seg_click(event *e, double amp_db) {
	seg_add(e, (segment){.kind = SEG_CLICK, .seconds = 0.004, .amp = db(amp_db)});
}

/* A pure interface tone, quick in and out: one note, or two at once */
static void beep(event *e, double hz, double hz2, double seconds, double amp_db) {
	seg_add(e, (segment){.kind = SEG_TONE, .f1 = hz, .f2 = hz2, .seconds = seconds, .amp = db(amp_db), .fade = 0.004, .decay = seconds});
}

/* A tone that rings: a glass bell, or with `harp` set, the note and its
 * octave, dying away */
static void ring(event *e, double hz, double seconds, double decay, double amp_db, int harp) {
	if (harp) {
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = hz, .f2 = hz * 2.0, .seconds = seconds, .amp = db(amp_db), .fade = 0.004, .decay = decay});
	} else {
		seg_add(e, (segment){.kind = SEG_BELL, .f1 = hz, .seconds = seconds, .amp = db(amp_db), .fade = 0.002, .decay = decay});
	}
}

/* A pad that swells in and out over `seconds`: up to four notes */
static void pad(event *e, double n1, double n2, double n3, double n4, double seconds, double amp_db, double cutoff) {
	seg_add(e, (segment){.kind = SEG_PAD, .f1 = n1, .f2 = n2, .f3 = n3, .f4 = n4, .seconds = seconds,
	                     .amp = db(amp_db), .fade = seconds / 3.0, .cutoff = cutoff});
}

/* ---- bytebeat: a whole song in one line of C ------------------------- */

/* The formulas, one byte a sample at 8 kHz. The classics are viznut's
 * and his commenters' from 2011; melody, saw and chirp take a number
 * of the style's own. */
enum { BB_MELODY, BB_42, BB_SAW, BB_CHIRP, BB_SIERPINSKI, BB_CRUNCH, BB_VIZNUT, BB_123, BB_CHIP, BB_63 };

static uint32_t shr(uint32_t x, uint32_t n) {
	return n < 32 ? x >> n : 0;
}

static int bytebeat(int formula, uint32_t t, uint32_t a) {
	uint32_t v;
	switch (formula) {
	case BB_MELODY: v = t * ((t >> 10 & a) | 4); break;          /* the 42 melody's shape, never resting */
	case BB_42: v = t * (42 & t >> 10); break;
	case BB_SAW: v = t * a; break;                               /* a saw at a x 31.25 Hz */
	case BB_CHIRP: v = t * t >> a; break;
	case BB_SIERPINSKI: v = t & t >> 8; break;
	case BB_CRUNCH: v = (t * 5 & t >> 7) | (t * 3 & t >> 10); break;
	case BB_VIZNUT: v = t * (t >> 5 | t >> 8); break;
	case BB_123: v = t * (t >> 11 & t >> 8 & 123 & t >> 3); break;
	case BB_CHIP: v = (t >> 6 | t | shr(t, t >> 16)) * 10 + (t >> 11 & 7); break;
	default: v = t * ((t >> 12 | t >> 8) & 63 & t >> 4); break;  /* BB_63 */
	}
	return (int)(v & 255u);
}

/* One sample of an event at `rate`, mono */
static double event_sample(event *e, uint32_t *rng, double rate) {
	if (e->at >= e->count) {
		e->active = 0;
		return 0.0;
	}
	segment *g = &e->seg[e->at];
	if (e->pos == 0) {
		e->len = (int)(g->seconds * rate);
		if (e->len < 1) e->len = 1;
		e->bit_left = 0;
		e->flip_left = (int)(g->flip * rate);
		e->syllable_left = 0;
		if (g->kind == SEG_NOISE) {
			e->c_hp = 1.0 - exp(-TAU * g->f1 / rate);
			e->c_lp = 1.0 - exp(-TAU * g->f2 / rate);
			e->norm = sqrt(3.0 * rate / 2.0 / (g->f2 - g->f1 > 50.0 ? g->f2 - g->f1 : 50.0));
		} else if (g->kind == SEG_MURMUR) {
			e->c_hp = 1.0 - exp(-TAU * 300.0 / rate);
			e->norm = 4.0 * sqrt(rate / LINE_RATE);
		} else if (g->kind == SEG_PAD) {
			e->c_lp = 1.0 - exp(-TAU * g->cutoff / rate);
		} else if (g->kind == SEG_KICK || g->kind == SEG_FM) {
			e->p1 = e->p2 = 0.0; /* every hit from the zero crossing, or it clicks */
		} else if (g->kind == SEG_BYTE) {
			e->c_hp = 1.0 - exp(-TAU * 25.0 / rate);
			e->c_lp = 1.0 - exp(-TAU * (g->cutoff > 0.0 ? g->cutoff : 2600.0) / rate);
		}
	}
	double t = e->pos;
	double fade = (g->fade > 0.0 ? g->fade : 0.005) * rate;
	double env = g->kind == SEG_CLICK ? 1.0 : fmin(1.0, fmin((t + 1.0) / fade, (e->len - t) / fade));
	if (g->decay > 0.0) env *= exp(-t / (g->decay * rate));
	double v = 0.0;
	switch (g->kind) {
	case SEG_TONE:
		e->p1 += TAU * g->f1 / rate;
		wrap(&e->p1);
		v = sin(e->p1);
		if (g->f2 > 0.0) {
			e->p2 += TAU * g->f2 / rate;
			wrap(&e->p2);
			v = 0.5 * (v + sin(e->p2));
		}
		if (g->flip > 0.0 && --e->flip_left <= 0) {
			e->flip_left = (int)(g->flip * rate);
			e->flip_sign = -e->flip_sign;
		}
		v *= e->flip_sign;
		break;
	case SEG_SWEEP:
		e->p1 += TAU * g->f1 * pow(g->f2 / g->f1, t / e->len) / rate;
		wrap(&e->p1);
		v = sin(e->p1);
		break;
	case SEG_FSK:
		if (--e->bit_left <= 0) {
			e->bit_left = (int)(rate / g->baud);
			e->fsk_hz = frand(rng) > 0.0 ? g->f1 : g->f2;
		}
		e->p1 += TAU * e->fsk_hz / rate;
		wrap(&e->p1);
		v = sin(e->p1);
		break;
	case SEG_CLICK:
		v = frand(rng) * exp(-t / (0.0008 * rate));
		break;
	case SEG_NOISE: {
		double n = frand(rng);
		e->hp += e->c_hp * (n - e->hp);
		e->lp1 += e->c_lp * ((n - e->hp) - e->lp1);
		e->lp2 += e->c_lp * (e->lp1 - e->lp2);
		v = e->lp2 * e->norm;
		if (g->baud > 0.0) v *= 0.5 + 0.5 * sin(TAU * g->baud * t / rate); /* a propeller's beat */
		break;
	}
	case SEG_MURMUR: {
		/* noise in the voice band, rising and falling at the pace of
		 * syllables, its colour moving: a conversation through a wall */
		if (--e->syllable_left <= 0) {
			e->syllable_left = (int)((0.08 + 0.17 * chance(rng)) * rate);
			e->murmur_target = chance(rng) < 0.3 ? 0.05 : 0.4 + 0.6 * chance(rng);
			e->c_lp = 1.0 - exp(-TAU * (600.0 + 800.0 * chance(rng)) / rate);
		}
		e->murmur_env += (1.0 - exp(-1.0 / (0.03 * rate))) * (e->murmur_target - e->murmur_env);
		double n = frand(rng);
		e->hp += e->c_hp * (n - e->hp);
		e->lp1 += e->c_lp * ((n - e->hp) - e->lp1);
		e->lp2 += e->c_lp * (e->lp1 - e->lp2);
		v = e->lp2 * e->norm * e->murmur_env;
		break;
	}
	case SEG_BELL: {
		/* glass: three inharmonic partials, the upper ones dying first */
		static const double ratio[3] = {1.0, 2.756, 5.404}, level[3] = {1.0, 0.35, 0.15}, speed[3] = {1.0, 2.0, 3.3};
		double d = (g->decay > 0.0 ? g->decay : 0.5) * rate;
		for (int k = 0; k < 3; k++) {
			e->ph[k] += TAU * g->f1 * ratio[k] / rate;
			wrap(&e->ph[k]);
			v += sin(e->ph[k]) * level[k] * exp(-t * (speed[k] - 1.0) / d);
		}
		break;
	}
	case SEG_PAD: {
		/* up to four notes, each two saws a few cents apart, through a
		 * soft low-pass: a slow synth pad */
		const double notes[4] = {g->f1, g->f2, g->f3, g->f4};
		double sum = 0.0;
		int n = 0;
		for (int k = 0; k < 4; k++) {
			if (notes[k] <= 0.0) continue;
			for (int side = 0; side < 2; side++) {
				double *ph = &e->ph[k * 2 + side];
				*ph += notes[k] * (side ? 1.0035 : 0.9965) / rate;
				if (*ph >= 1.0) *ph -= 1.0;
				sum += 2.0 * *ph - 1.0;
			}
			n++;
		}
		e->lp1 += e->c_lp * (sum - e->lp1);
		e->lp2 += e->c_lp * (e->lp1 - e->lp2);
		v = n ? e->lp2 * 1.6 / n : 0.0;
		if (g->baud > 0.0) v *= 0.65 + 0.35 * sin(TAU * g->baud * t / rate); /* an engine's throb */
		break;
	}
	case SEG_PLUCK: {
		/* a saw through a low-pass that closes fast: a trance pluck */
		e->p1 += g->f1 / rate;
		if (e->p1 >= 1.0) e->p1 -= 1.0;
		double d = (g->decay > 0.0 ? g->decay : 0.2) * rate;
		double fc = 180.0 + (g->cutoff - 180.0) * exp(-t / (0.35 * d));
		double c = 1.0 - exp(-TAU * fc / rate);
		e->lp1 += c * ((2.0 * e->p1 - 1.0) - e->lp1);
		e->lp2 += c * (e->lp1 - e->lp2);
		v = e->lp2 * 1.5;
		break;
	}
	case SEG_WHOOSH: {
		/* noise through a band that sweeps from f1 to f2: wind past a mast */
		double fc = g->f1 * pow(g->f2 / g->f1, t / e->len);
		double n = frand(rng);
		e->hp += (1.0 - exp(-TAU * fc * 0.6 / rate)) * (n - e->hp);
		double c = 1.0 - exp(-TAU * fc * 1.6 / rate);
		e->lp1 += c * ((n - e->hp) - e->lp1);
		e->lp2 += c * (e->lp1 - e->lp2);
		v = e->lp2 * sqrt(3.0 * rate / 2.0 / fc) * 0.8;
		break;
	}
	case SEG_CREAK: {
		/* a low saw bending from f1 to f2 through a low-pass at 400 Hz,
		 * its level catching and slipping: steel under strain */
		double f = g->f1 * pow(g->f2 / g->f1, t / e->len);
		e->p1 += f / rate;
		if (e->p1 >= 1.0) e->p1 -= 1.0;
		if (--e->flutter_left <= 0) {
			e->flutter_left = (int)((0.012 + 0.03 * chance(rng)) * rate);
			e->flutter_target = 0.25 + 0.75 * chance(rng);
		}
		e->flutter += (1.0 - exp(-1.0 / (0.004 * rate))) * (e->flutter_target - e->flutter);
		double c = 1.0 - exp(-TAU * 400.0 / rate);
		e->lp1 += c * ((2.0 * e->p1 - 1.0) - e->lp1);
		e->lp2 += c * (e->lp1 - e->lp2);
		v = e->lp2 * 2.0 * e->flutter;
		break;
	}
	case SEG_KICK: {
		/* a sine whose pitch drops fast from f1 to f2, a click on top: a
		 * kick drum, or higher up a tom */
		double f = g->f2 + (g->f1 - g->f2) * exp(-t / (0.03 * rate));
		e->p1 += TAU * f / rate;
		wrap(&e->p1);
		v = sin(e->p1) + g->f3 * frand(rng) * exp(-t / (0.0012 * rate));
		break;
	}
	case SEG_FM: {
		/* a sine bent by another at f2 times its pitch, the bend (f3)
		 * dying away faster than the note: at an inharmonic ratio, metal
		 * struck */
		double d = (g->decay > 0.0 ? g->decay : 0.2) * rate;
		e->p1 += TAU * g->f1 / rate;
		wrap(&e->p1);
		e->p2 += TAU * g->f1 * g->f2 / rate;
		wrap(&e->p2);
		v = sin(e->p1 + g->f3 * exp(-t / (0.4 * d)) * sin(e->p2));
		break;
	}
	case SEG_BYTE: {
		/* a bytebeat formula: t is the voice's clock, the same for every
		 * formula, so each window is onto a song that's been playing since
		 * the voice started; a saw's or a chirp's t is the segment's own.
		 * Memory pressure holds t for longer and takes the low bits. */
		uint32_t tt = g->formula == BB_SAW || g->formula == BB_CHIRP ? (uint32_t)e->pos : e->t;
		tt -= tt % (uint32_t)(e->crush_hold > 0 ? e->crush_hold : 1);
		double x = (bytebeat(g->formula, tt, (uint32_t)g->a) & e->crush_mask) / 128.0 - 1.0;
		if (e->pos == 0) e->dc = x; /* a formula resting on 0 is silence, not a thump */
		e->dc += e->c_hp * (x - e->dc);
		e->lp1 += e->c_lp * ((x - e->dc) - e->lp1);
		e->lp2 += e->c_lp * (e->lp1 - e->lp2);
		v = e->lp2 * 1.6;
		break;
	}
	default:
		break;
	}
	e->t++;
	if (++e->pos >= e->len) {
		e->at++;
		e->pos = 0;
	}
	return v * env * g->amp;
}

/* ---- each style's notes ----------------------------------------------- */

/* uplink: A minor pentatonic, two octaves up from A4 */
static const double UPLINK_SCALE[10] = {440.0, 523.25, 587.33, 659.26, 783.99, 880.0, 1046.5, 1174.66, 1318.51, 1567.98};
/* hacker: E phrygian, from E5: dark, a half step over the root */
static const double HACKER_SCALE[10] = {659.26, 698.46, 783.99, 880.0, 987.77, 1046.5, 1174.66, 1318.51, 1396.91, 1567.98};
/* deep: G minor pentatonic from G2, low enough for sonar */
static const double DEEP_SCALE[10] = {98.0, 116.54, 130.81, 146.83, 174.61, 196.0, 233.08, 261.63, 293.66, 349.23};
/* techno: F phrygian from F3, the flat second its darkness */
static const double TECHNO_SCALE[10] = {174.61, 185.0, 207.65, 233.08, 261.63, 277.18, 311.13, 349.23, 369.99, 415.30};
/* bytebeat: each core's mask in t*((t>>10&M)|4), the 42 melody's shape;
 * every note is a harmonic of 31.25 Hz, the byte's wrap at 8 kHz */
static const int BYTE_MASK[10] = {42, 21, 56, 13, 38, 25, 50, 11, 44, 19};
/* phreak: MF, the in-band signalling a blue box spoke; a digit's two tones */
static const double MF[10][2] = {
	{1300, 1500}, {700, 900}, {700, 1100}, {900, 1100}, {700, 1300},
	{900, 1300}, {1100, 1300}, {700, 1500}, {900, 1500}, {1100, 1500},
};

/* a core's own note in each style */
static double core_note(const hum_synth *s, int core) {
	switch (s->style) {
	case SYN_HUM_UPLINK: return UPLINK_SCALE[core % 10];
	case SYN_HUM_HACKER: return HACKER_SCALE[core % 10];
	case SYN_HUM_DEEP: return DEEP_SCALE[core % 10];
	case SYN_HUM_TECHNO: return TECHNO_SCALE[core % 10];
	default: return F0 * (core % MAX_VOICES + 2); /* harmonic: the core's harmonic */
	}
}

/* techno's drum machine. A kick, or with other notes a tom */
static void kick(event *e, double from, double to, double seconds, double decay, double amp_db) {
	seg_add(e, (segment){.kind = SEG_KICK, .f1 = from, .f2 = to, .f3 = 0.25, .seconds = seconds, .amp = db(amp_db), .fade = 0.001, .decay = decay});
}

/* A closed hi-hat */
static void hat(event *e, double seconds, double decay, double amp_db) {
	seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 6000.0, .f2 = 11000.0, .seconds = seconds, .amp = db(amp_db), .fade = 0.001, .decay = decay});
}

/* Struck metal: a note bent by another at `ratio` times it; `index` is
 * how hard it's struck */
static void metal(event *e, double hz, double ratio, double index, double seconds, double decay, double amp_db) {
	seg_add(e, (segment){.kind = SEG_FM, .f1 = hz, .f2 = ratio, .f3 = index, .seconds = seconds, .amp = db(amp_db),
	                     .fade = 0.001, .decay = decay});
}

/* A dissonant stab, F with a flat ninth rubbing the minor third, for the
 * echo to carry off */
static void stab(event *e, double seconds, double amp_db, double cutoff) {
	seg_add(e, (segment){.kind = SEG_PAD, .f1 = 87.31, .f2 = 130.81, .f3 = 185.0, .f4 = 207.65, .seconds = seconds,
	                     .amp = db(amp_db), .fade = 0.004, .decay = 0.09, .cutoff = cutoff});
}

/* bytebeat: a window `seconds` long onto a formula */
static void byte_window(event *e, int formula, int a, double seconds, double amp_db, double fade, double decay, double cutoff) {
	seg_add(e, (segment){.kind = SEG_BYTE, .formula = formula, .a = a, .seconds = seconds, .amp = db(amp_db),
	                     .fade = fade, .decay = decay, .cutoff = cutoff});
}

/* uplink's password breaker: characters cycling, `ticks` fast high ticks,
 * then one locks, a glass tone at the core's note */
static void uplink_breaker(hum_synth *s, event *e, int core, int step, int ticks) {
	static const double cycling[6] = {1760.0, 1975.5, 2093.0, 2349.3, 2637.0, 2793.8};
	for (int i = 0; i < ticks; i++) {
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = cycling[(int)(chance(&s->rng) * 6.0)], .seconds = 0.018,
		                     .amp = db(-28.0), .fade = 0.002});
		seg_gap(e, 0.022);
	}
	ring(e, core_note(s, core), 1.5, 0.5, -22.0 - (15 - step), 0);
}

/* ---- what each style plays -------------------------------------------- */

/* CPU, now and then: a core did some work */
static void ev_ping(hum_synth *s, int core, int step) {
	event *e = event_new(s, EV_PING, 0, core_pan(s, core));
	if (!e) return;
	char line[48];
	const char *what = "a tone";
	switch (s->style) {
	case SYN_HUM_UPLINK:
		uplink_breaker(s, e, core, step, 5 + (int)(chance(&s->rng) * 8.0));
		what = "password breaker";
		break;
	case SYN_HUM_PHREAK: {
		/* the core's digit in MF, soft: a switchboard ticking over */
		int digit = (core + 1) % 10;
		beep(e, MF[digit][0], MF[digit][1], 0.07, step_db(step));
		what = "an MF digit";
		break;
	}
	case SYN_HUM_HACKER:
		/* a precise tick, then a glint at the core's note */
		seg_add(e, (segment){.kind = SEG_SWEEP, .f1 = 3200.0, .f2 = 1600.0, .seconds = 0.02,
		                     .amp = db(step_db(step) - 2.0), .fade = 0.001, .decay = 0.006});
		seg_gap(e, 0.01);
		ring(e, core_note(s, core), 0.8, 0.22, step_db(step), 0);
		what = "a tick and a glint";
		break;
	case SYN_HUM_DEEP:
		/* a low sonar ping on the core's note, with its octave so small
		 * speakers carry it, ringing out into the hull */
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = core_note(s, core), .f2 = core_note(s, core) * 2.0, .seconds = 2.5,
		                     .amp = db(step_db(step)), .fade = 0.012, .decay = 0.9});
		what = "a sonar ping";
		break;
	case SYN_HUM_TECHNO:
		/* struck metal at the core's note; a harder step strikes it harder */
		metal(e, core_note(s, core), 1.41, 0.8 + 0.25 * step, sixteenths(3), 0.09 + 0.006 * step, step_db(step) + 1.0);
		what = "struck metal";
		break;
	case SYN_HUM_BYTEBEAT: {
		/* a window onto the core's own melody, named by its formula */
		static char formula[32];
		byte_window(e, BB_MELODY, BYTE_MASK[core % 10], 0.18 + 0.2 * chance(&s->rng), step_db(step), 0.01, 0.2, 2400.0);
		snprintf(formula, sizeof(formula), "t*((t>>10&%d)|4)", BYTE_MASK[core % 10]);
		what = formula;
		break;
	}
	default:
		/* a harp note on the core's harmonic */
		ring(e, core_note(s, core), 2.5, 0.9, step_db(step), 1);
		what = "a harp note";
		break;
	}
	snprintf(line, sizeof(line), "cpu: %s, core %d", what, core);
	say(s, line);
}

/* CPU, held: a pegged core's note as a pad that breathes, started again
 * as it ends, for as long as the core stays pegged */
static void ev_hold_core(hum_synth *s, int core) {
	event *e = event_new(s, EV_HOLD + core, 1, core_pan(s, core));
	if (!e) return;
	double n = core_note(s, core);
	switch (s->style) {
	case SYN_HUM_UPLINK: pad(e, n / 4.0, n / 4.0 * 1.5, 0, 0, 6.0, -30.0, 900.0); break;
	case SYN_HUM_PHREAK: /* a dial tone, breathing */
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = 350.0, .f2 = 440.0, .seconds = 6.0, .amp = db(-36.0), .fade = 2.0});
		break;
	case SYN_HUM_HACKER: pad(e, n / 4.0, n / 4.0 * 1.189, 0, 0, 6.0, -32.0, 600.0); break; /* a dark minor third */
	case SYN_HUM_DEEP: /* the engine: the core's note an octave down and its fifth, throbbing */
		seg_add(e, (segment){.kind = SEG_PAD, .f1 = n / 2.0, .f2 = n / 2.0 * 1.5, .seconds = 6.0, .amp = db(-38.0),
		                     .fade = 2.0, .cutoff = 400.0, .baud = 1.2});
		break;
	case SYN_HUM_BYTEBEAT: /* the core's melody playing on, breathing */
		byte_window(e, BB_MELODY, BYTE_MASK[core % 10], 6.0, -36.0, 2.0, 0.0, 1200.0);
		break;
	default: pad(e, n, n * 2.0, 0, 0, 6.0, -32.0, 1200.0); break;
	}
}

/* techno, held: the groove, a layer per pegged core (techno_layer's
 * caller counts them), each a bar long and coming in on the bar. A bar
 * ends a little short, so the tick after it ends starts the next on the
 * very next bar line. It's the one held thing that isn't a pad, but it's
 * still short sounds: the same bar, the same level, however hard the
 * cores work. */
static void techno_layer(hum_synth *s, int layer) {
	static const double pans[4] = {0.0, 0.0, 0.25, -0.2};
	event *e = event_new(s, EV_HOLD + layer, 1, pans[layer]);
	if (!e) return;
	const double beat = sixteenths(4), sixteenth = sixteenths(1);
	for (int b = 0; b < 4; b++) {
		double end = b == 3 ? 0.11 : 0.0;
		switch (layer) {
		case 0: kick(e, 120.0, 45.0, beat - end, 0.22, -26.0); break;
		case 1: /* a rolling bass, the second and third sixteenths of the beat */
			seg_gap(e, sixteenth);
			for (int i = 0; i < 2; i++) {
				seg_add(e, (segment){.kind = SEG_PLUCK, .f1 = 87.31, .seconds = sixteenth, .amp = db(-30.0), .fade = 0.003,
				                     .cutoff = 520.0, .decay = 0.06});
			}
			seg_gap(e, sixteenth - end);
			break;
		case 2: /* hats on every sixteenth, the off-beat harder */
			for (int i = 0; i < 4; i++) hat(e, sixteenth - (i == 3 ? end : 0.0), 0.02, i == 2 ? -31.0 : -39.0);
			break;
		default: /* a clank on two and four */
			if (b % 2) {
				metal(e, 140.0, 1.41, 4.0, beat - end, 0.12, -30.0);
			} else {
				seg_gap(e, beat - end);
			}
			break;
		}
	}
}

/* techno, tasks waiting for a CPU: a rumble under the beat, low noise
 * ducking on each beat and swelling between, a bar at a time */
static void techno_rumble(hum_synth *s) {
	event *e = event_new(s, EV_RUMBLE, 1, 0.0);
	if (!e) return;
	for (int b = 0; b < 4; b++) {
		seg_gap(e, sixteenths(1));
		seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 45.0, .f2 = 160.0, .seconds = sixteenths(3) - (b == 3 ? 0.11 : 0.0),
		                     .amp = db(-36.0), .fade = 0.12});
	}
}

/* The CPU's percentage, spelled out */
static void ev_readout(hum_synth *s, int percent) {
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	event *e = event_new(s, EV_READOUT, 1, 0.4 * frand(&s->rng));
	if (!e) return;
	char line[80];
	int tens = (percent + 5) / 10 < 1 ? 1 : (percent + 5) / 10;
	switch (s->style) {
	case SYN_HUM_UPLINK:
		/* bounce beeps, a node per 10%, rising up the scale */
		for (int i = 0; i < tens; i++) {
			beep(e, UPLINK_SCALE[i], 0, 0.08, -28.0);
			seg_gap(e, 0.12);
		}
		snprintf(line, sizeof(line), "readout: %d bounce beep%s (cpu %d%%)", tens, tens == 1 ? "" : "s", percent);
		break;
	case SYN_HUM_PHREAK: {
		/* KP, the digits, ST */
		char digits[8];
		snprintf(digits, sizeof(digits), "%d", percent);
		beep(e, 1100, 1700, 0.1, -28.0);
		seg_gap(e, 0.06);
		for (const char *d = digits; *d; d++) {
			beep(e, MF[*d - '0'][0], MF[*d - '0'][1], 0.06, -28.0);
			seg_gap(e, 0.06);
		}
		beep(e, 1500, 1700, 0.1, -28.0);
		snprintf(line, sizeof(line), "readout: MF KP %s ST (cpu %d%%)", digits, percent);
		break;
	}
	case SYN_HUM_HACKER: {
		/* a rising sweep, then seven bits as glass tones: high for 1 */
		seg_add(e, (segment){.kind = SEG_SWEEP, .f1 = 600, .f2 = 1800, .seconds = 0.06, .amp = db(-32.0), .fade = 0.004});
		seg_gap(e, 0.08);
		char bits[8];
		for (int b = 6; b >= 0; b--) {
			int one = (percent >> b) & 1;
			bits[6 - b] = one ? '1' : '0';
			ring(e, one ? 1318.51 : 659.26, 0.1, 0.08, -30.0, 0);
			seg_gap(e, 0.04);
		}
		bits[7] = '\0';
		snprintf(line, sizeof(line), "readout: binary %s (cpu %d%%)", bits, percent);
		break;
	}
	case SYN_HUM_DEEP:
		/* depth soundings: a short low ping per 10% */
		for (int i = 0; i < tens; i++) {
			seg_add(e, (segment){.kind = SEG_TONE, .f1 = 196.0, .f2 = 392.0, .seconds = 0.3, .amp = db(-28.0), .fade = 0.01, .decay = 0.15});
			seg_gap(e, 0.25);
		}
		snprintf(line, sizeof(line), "readout: %d sounding%s (cpu %d%%)", tens, tens == 1 ? "" : "s", percent);
		break;
	case SYN_HUM_TECHNO:
		/* low metal tolls on the eighths, one per 10% */
		for (int i = 0; i < tens; i++) metal(e, 174.61, 1.41, 2.5, sixteenths(2), 0.2, -28.0);
		snprintf(line, sizeof(line), "readout: %d toll%s (cpu %d%%)", tens, tens == 1 ? "" : "s", percent);
		break;
	case SYN_HUM_BYTEBEAT:
		/* saws rising a harmonic of 31.25 Hz at a time from t*8, one per 10% */
		for (int i = 0; i < tens; i++) {
			byte_window(e, BB_SAW, 8 + 2 * i, 0.09, -30.0, 0.004, 0.0, 2600.0);
			seg_gap(e, 0.11);
		}
		snprintf(line, sizeof(line), "readout: %d saw%s, t*8 up (cpu %d%%)", tens, tens == 1 ? "" : "s", percent);
		break;
	default:
		/* up the series, a note per 10% */
		for (int i = 0; i < tens; i++) ring(e, F0 * (i + 2), 0.14, 0.3, -28.0, 1);
		snprintf(line, sizeof(line), "readout: arpeggio of %d (cpu %d%%)", tens, percent);
		break;
	}
	say(s, line);
}

/* Network: now and then, and while heavy, again as each ends */
static void ev_net(hum_synth *s) {
	event *e = event_new(s, EV_NET, 1, 0.5 * frand(&s->rng));
	if (!e) return;
	uint32_t *r = &s->rng;
	int heavy = s->hot_fsk >= HOLD_TICKS;
	const char *what = NULL;
	switch (s->style) {
	case SYN_HUM_UPLINK:
		if (!heavy) {
			/* data going by: a few quick blips high on the scale */
			int blips = 3 + (int)(chance(r) * 4.0);
			for (int i = 0; i < blips; i++) {
				beep(e, UPLINK_SCALE[5 + (int)(chance(r) * 5.0)], 0, 0.03, -28.0);
				seg_gap(e, 0.03);
			}
			what = "network: data";
		} else {
			/* sixteenths at 110 bpm, a random walk up and down the scale */
			int at = 3 + (int)(chance(r) * 4.0);
			for (int i = 0; i < 16; i++) {
				seg_add(e, (segment){.kind = SEG_PLUCK, .f1 = UPLINK_SCALE[at], .seconds = 60.0 / 110.0 / 4.0,
				                     .amp = db(-30.0), .cutoff = 2600.0, .decay = 0.18});
				at += (int)(chance(r) * 5.0) - 2;
				at = at < 0 ? 1 : at > 9 ? 8 : at;
			}
		}
		break;
	case SYN_HUM_PHREAK: {
		/* a scrap of a modem, soft */
		double pick = chance(r), amp = -32.0 + 2.0 * frand(r);
		if (pick < 0.55) {
			seg_add(e, (segment){.kind = SEG_FSK, .f1 = 1200, .f2 = 2200, .baud = 1200, .seconds = 0.15 + 0.35 * chance(r), .amp = db(amp), .fade = 0.02});
			what = "modem: Bell 202 burst";
		} else if (pick < 0.8) {
			seg_add(e, (segment){.kind = SEG_TONE, .f1 = 2100, .seconds = 0.5 + 0.6 * chance(r), .amp = db(amp - 2.0), .fade = 0.05, .flip = 0.45});
			what = "modem: answer tone";
		} else {
			seg_add(e, (segment){.kind = SEG_FSK, .f1 = 1070, .f2 = 1270, .baud = 300, .seconds = 0.3, .amp = db(amp), .fade = 0.02});
			seg_gap(e, 0.08);
			seg_add(e, (segment){.kind = SEG_FSK, .f1 = 2025, .f2 = 2225, .baud = 300, .seconds = 0.3, .amp = db(amp), .fade = 0.02});
			what = "modem: Bell 103 handshake";
		}
		if (heavy) seg_gap(e, 0.8);
		break;
	}
	case SYN_HUM_HACKER: {
		/* data shimmer: a breath of high noise, and a few soft pips */
		seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 2000, .f2 = 7000, .seconds = 0.14, .amp = db(-36.0), .fade = 0.05});
		int pips = 2 + (int)(chance(r) * 3.0);
		for (int i = 0; i < pips; i++) {
			beep(e, HACKER_SCALE[5 + (int)(chance(r) * 5.0)] * 2.0, 0, 0.03, -34.0);
			seg_gap(e, 0.05);
		}
		if (heavy) seg_gap(e, 0.4);
		what = "network: data shimmer";
		break;
	}
	case SYN_HUM_DEEP:
		/* distant propeller wash: low noise beating at the blade rate */
		seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 150.0, .f2 = 700.0, .seconds = heavy ? 2.0 : 1.5 + 1.5 * chance(r),
		                     .amp = db(heavy ? -36.0 : -34.0), .fade = 0.6, .baud = 3.0 + 3.0 * chance(r)});
		if (heavy) seg_gap(e, 0.2);
		what = "network: propeller wash";
		break;
	case SYN_HUM_TECHNO:
		if (!heavy) {
			/* bursts of static on the sixteenths, short and long, now and
			 * then one skipped */
			int bursts = 2 + (int)(chance(r) * 4.0);
			for (int i = 0; i < bursts; i++) {
				double len = sixteenths(1) * (0.25 + 0.6 * chance(r));
				if (i && chance(r) < 0.3) len = 0.0;
				if (len > 0.0) {
					seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 1200.0, .f2 = 3500.0, .seconds = len,
					                     .amp = db(-34.0 + 3.0 * frand(r)), .fade = 0.002});
				}
				seg_gap(e, sixteenths(1) - len);
			}
			what = "network: static";
		} else {
			/* static gated on every sixteenth for a bar, the off-beats
			 * harder, counted from the grid so a bar started late still
			 * lands them on the off-beats */
			uint64_t first = grid_next(s, 1);
			for (int i = 0; i < 16; i++) {
				seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 1200.0, .f2 = 3500.0, .seconds = 0.03,
				                     .amp = db((first + i) % 4 == 2 ? -31.0 : -38.0), .fade = 0.002});
				seg_gap(e, sixteenths(1) - 0.03);
			}
		}
		break;
	case SYN_HUM_BYTEBEAT:
		if (!heavy) {
			byte_window(e, BB_SIERPINSKI, 0, 0.12 + 0.25 * chance(r), -34.0, 0.01, 0.0, 3000.0);
			what = "network: t&t>>8";
		} else {
			byte_window(e, BB_CRUNCH, 0, 2.0, -38.0, 0.3, 0.0, 2200.0);
			seg_gap(e, 0.3);
		}
		break;
	default: {
		/* sparkles high on the series, the 11th to the 16th */
		int n = 2 + (int)(chance(r) * 3.0);
		for (int i = 0; i < n; i++) {
			ring(e, F0 * (11 + (int)(chance(r) * 6.0)), 0.09, 0.25, -34.0, 0);
		}
		if (heavy) seg_gap(e, 0.5);
		what = "network: sparkles";
		break;
	}
	}
	if (what && !heavy) say(s, what);
}

/* Disk: now and then, and while busy, again as each ends */
static void ev_disk(hum_synth *s, int percent) {
	event *e = event_new(s, EV_DISK, 1, 0.3 * frand(&s->rng));
	if (!e) return;
	int busy = s->hot_floor >= HOLD_TICKS;
	char line[64];
	switch (s->style) {
	case SYN_HUM_UPLINK:
		/* the file copier: a progress bar filling, eight blips rising */
		for (int i = 0; i < 8; i++) {
			beep(e, 523.25 * pow(2.0, i / 7.0), 0, 0.03, -28.0);
			seg_gap(e, 0.05);
		}
		if (busy) seg_gap(e, 0.4);
		snprintf(line, sizeof(line), "disk: copying (disk %d%%)", percent);
		break;
	case SYN_HUM_PHREAK:
		/* a relay picking up */
		seg_click(e, -28.0);
		seg_gap(e, 0.04);
		seg_click(e, -32.0);
		if (busy) seg_gap(e, 1.2);
		snprintf(line, sizeof(line), "disk: a relay (disk %d%%)", percent);
		break;
	case SYN_HUM_HACKER:
		/* a low pulse */
		seg_add(e, (segment){.kind = SEG_SWEEP, .f1 = 90.0, .f2 = 50.0, .seconds = 0.16, .amp = db(-30.0), .fade = 0.003, .decay = 0.08});
		seg_click(e, -40.0);
		if (busy) seg_gap(e, 1.0);
		snprintf(line, sizeof(line), "disk: a pulse (disk %d%%)", percent);
		break;
	case SYN_HUM_DEEP:
		/* the hull creaking */
		seg_add(e, (segment){.kind = SEG_CREAK, .f1 = 180.0, .f2 = 120.0, .seconds = 0.6 + 0.6 * chance(&s->rng), .amp = db(-32.0), .fade = 0.05});
		if (busy) seg_gap(e, 1.5);
		snprintf(line, sizeof(line), "disk: the hull creaks (disk %d%%)", percent);
		break;
	case SYN_HUM_TECHNO:
		/* a machine clank, now and then struck twice */
		seg_click(e, -36.0);
		metal(e, 70.0, 2.76, 5.0, sixteenths(1), 0.1, -28.0);
		if (chance(&s->rng) < 0.4) metal(e, 70.0, 2.76, 3.0, sixteenths(2), 0.08, -34.0);
		if (busy) seg_gap(e, sixteenths(4));
		snprintf(line, sizeof(line), "disk: a clank (disk %d%%)", percent);
		break;
	case SYN_HUM_BYTEBEAT:
		byte_window(e, BB_VIZNUT, 0, 0.1 + 0.1 * chance(&s->rng), -33.0, 0.005, 0.0, 2400.0);
		if (busy) seg_gap(e, 0.6);
		snprintf(line, sizeof(line), "disk: t*(t>>5|t>>8) (disk %d%%)", percent);
		break;
	default:
		/* the fundamental and its octave, swelling and fading */
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = F0, .f2 = F0 * 2.0, .seconds = 2.5, .amp = db(-32.0), .fade = 0.8});
		snprintf(line, sizeof(line), "disk: the fundamental swells (disk %d%%)", percent);
		break;
	}
	if (!busy) say(s, line);
}

/* Memory pressure: a sound that comes faster the worse it gets */
static void ev_memory(hum_synth *s) {
	event *e = event_new(s, EV_MEMORY, s->style == SYN_HUM_DEEP, 0.0); /* deep's groans one at a time */
	if (!e) return;
	switch (s->style) {
	case SYN_HUM_UPLINK: beep(e, 1567.98, 0, 0.06, -28.0); break; /* the trace tracker */
	case SYN_HUM_PHREAK: /* a burst of the busy signal */
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = 480.0, .f2 = 620.0, .seconds = 0.25, .amp = db(-32.0), .fade = 0.01});
		break;
	case SYN_HUM_HACKER: ring(e, 1200.0, 0.7, 0.2, -30.0, 0); break; /* sonar */
	case SYN_HUM_DEEP: /* a pressure groan, deep */
		seg_add(e, (segment){.kind = SEG_CREAK, .f1 = 95.0, .f2 = 62.0, .seconds = 1.0, .amp = db(-30.0), .fade = 0.08});
		break;
	case SYN_HUM_TECHNO: stab(e, sixteenths(2), -31.0, 1000.0); break; /* a dissonant stab into the echo */
	case SYN_HUM_BYTEBEAT: byte_window(e, BB_CHIRP, 6, 0.2, -32.0, 0.005, 0.0, 3000.0); break; /* t*t>>6 */
	default: /* two notes 3 Hz apart, beating */
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = F0 * 8.0, .f2 = F0 * 8.0 + 3.0, .seconds = 1.2, .amp = db(-32.0), .fade = 0.01, .decay = 0.6});
		break;
	}
}

static void ev_error(hum_synth *s) {
	event *e = event_new(s, EV_ERROR, 1, 0.0);
	if (!e) return;
	switch (s->style) {
	case SYN_HUM_UPLINK: /* access denied */
		beep(e, 880.0, 0, 0.12, -24.0);
		seg_gap(e, 0.04);
		beep(e, 587.33, 0, 0.2, -24.0);
		break;
	case SYN_HUM_PHREAK: /* reorder: the fast busy signal, twice */
		for (int i = 0; i < 2; i++) {
			seg_add(e, (segment){.kind = SEG_TONE, .f1 = 480.0, .f2 = 620.0, .seconds = 0.2, .amp = db(-26.0), .fade = 0.01});
			seg_gap(e, 0.15);
		}
		break;
	case SYN_HUM_HACKER:
		ring(e, 392.0, 0.12, 0.2, -26.0, 1);
		seg_gap(e, 0.03);
		ring(e, 293.66, 0.5, 0.25, -26.0, 1);
		break;
	case SYN_HUM_DEEP: /* a low klaxon pulse, twice */
		for (int i = 0; i < 2; i++) {
			seg_add(e, (segment){.kind = SEG_PAD, .f1 = 110.0, .f2 = 116.54, .seconds = 0.35, .amp = db(-26.0), .fade = 0.03, .cutoff = 700.0});
			seg_gap(e, 0.15);
		}
		break;
	case SYN_HUM_TECHNO: { /* an impact: a low boom and, over it, a metal scrape */
		kick(e, 90.0, 32.0, 1.5, 0.45, -24.0);
		event *scrape = event_new(s, EV_ERROR, 0, 0.25);
		if (scrape) metal(scrape, 220.0, 2.76, 7.0, 1.2, 0.35, -28.0);
		break;
	}
	case SYN_HUM_BYTEBEAT: /* two saws falling a fifth */
		byte_window(e, BB_SAW, 12, 0.12, -26.0, 0.004, 0.0, 2600.0);
		seg_gap(e, 0.03);
		byte_window(e, BB_SAW, 8, 0.3, -26.0, 0.004, 0.2, 2600.0);
		break;
	default: /* falls from the 16th harmonic to the 12th */
		ring(e, F0 * 16, 0.12, 0.3, -26.0, 1);
		ring(e, F0 * 12, 0.8, 0.4, -26.0, 1);
		break;
	}
	say(s, "log error");
}

static void ev_inbound(hum_synth *s) {
	event *e = event_new(s, EV_INBOUND, 1, 0.0);
	if (!e) return;
	switch (s->style) {
	case SYN_HUM_UPLINK: {
		static const double rising[3] = {1046.5, 1318.51, 1567.98};
		for (int i = 0; i < 3; i++) {
			beep(e, rising[i], 0, 0.07, -26.0);
			seg_gap(e, 0.07);
		}
		ring(e, 880.0, 1.5, 0.6, -26.0, 0);
		break;
	}
	case SYN_HUM_PHREAK: /* someone's calling: a burst of ringback */
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = 440.0, .f2 = 480.0, .seconds = 0.8, .amp = db(-28.0), .fade = 0.03});
		break;
	case SYN_HUM_HACKER: /* a sonar sweep and its ping */
		seg_add(e, (segment){.kind = SEG_SWEEP, .f1 = 400, .f2 = 1600, .seconds = 0.25, .amp = db(-30.0), .fade = 0.01});
		ring(e, 1600.0, 0.6, 0.15, -28.0, 0);
		break;
	case SYN_HUM_DEEP: /* contact: a ping, and its return */
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = 220.0, .f2 = 440.0, .seconds = 0.9, .amp = db(-28.0), .fade = 0.01, .decay = 0.35});
		seg_gap(e, 0.4);
		seg_add(e, (segment){.kind = SEG_TONE, .f1 = 220.0, .f2 = 440.0, .seconds = 0.9, .amp = db(-38.0), .fade = 0.01, .decay = 0.35});
		break;
	case SYN_HUM_TECHNO: /* a swell, and a high ping ringing into the echo */
		seg_add(e, (segment){.kind = SEG_WHOOSH, .f1 = 300.0, .f2 = 3000.0, .seconds = sixteenths(4), .amp = db(-34.0), .fade = 0.2});
		metal(e, 1396.91, 2.0, 1.2, 1.5, 0.5, -28.0);
		break;
	case SYN_HUM_BYTEBEAT: { /* t*16, t*20, t*24: 4:5:6, a major triad */
		for (int i = 0; i < 3; i++) {
			byte_window(e, BB_SAW, 16 + 4 * i, i == 2 ? 0.3 : 0.09, -26.0, 0.004, i == 2 ? 0.2 : 0.0, 2600.0);
			if (i < 2) seg_gap(e, 0.05);
		}
		break;
	}
	default: /* rises from the 8th harmonic to the 10th */
		ring(e, F0 * 8, 0.12, 0.3, -26.0, 1);
		ring(e, F0 * 10, 0.8, 0.5, -26.0, 1);
		break;
	}
	say(s, "inbound connection");
}

/* Uplink, network: connecting through the bounces when traffic starts,
 * and the line dropping when it stops */
static void ev_link(hum_synth *s, int up) {
	event *e = event_new(s, EV_LINK, 0, 0.3 * frand(&s->rng));
	if (!e) return;
	if (up) {
		static const double bounce[3] = {1046.5, 1174.66, 1318.51};
		int nodes = 3 + (int)(chance(&s->rng) * 4.0);
		for (int i = 0; i < nodes; i++) {
			beep(e, bounce[(int)(chance(&s->rng) * 3.0)], 0, 0.07, -30.0);
			seg_gap(e, 0.11);
		}
		beep(e, 880.0, 0, 0.1, -26.0);
		beep(e, 1318.51, 0, 0.16, -26.0);
		say(s, "network: connected");
	} else {
		beep(e, 1318.51, 0, 0.09, -28.0);
		beep(e, 880.0, 0, 0.09, -28.0);
		beep(e, 659.26, 0, 0.16, -28.0);
		say(s, "network: disconnected");
	}
}

/* The atmosphere, carrying nothing */
static void ev_atmos(hum_synth *s, int which) {
	which %= SYN_HUM_STYLES[s->style].atmos_kinds;
	event *e = event_new(s, EV_ATMOS + which, 1, 0.7 * frand(&s->rng)); /* each kind alone, kinds may overlap */
	if (!e) return;
	uint32_t *r = &s->rng;
	const char *what = "";
	switch (s->style) {
	case SYN_HUM_UPLINK:
		switch (which) {
		case 0:
			pad(e, 110.0, 164.81, 196.0, 246.94, 9.0, -28.0, 750.0);
			what = "a pad: A minor 9";
			break;
		case 5:
			pad(e, 87.31, 130.81, 164.81, 220.0, 9.0, -28.0, 750.0);
			what = "a pad: F major 7";
			break;
		case 1:
			seg_add(e, (segment){.kind = SEG_WHOOSH, .f1 = 250.0, .f2 = 2800.0, .seconds = 3.0, .amp = db(-30.0), .fade = 1.2});
			what = "a far sweep";
			break;
		case 2:
			seg_add(e, (segment){.kind = SEG_TONE, .f1 = 55.0, .f2 = 110.0, .seconds = 4.0, .amp = db(-28.0), .fade = 1.8});
			what = "a deep swell";
			break;
		case 3: {
			/* three glass tones, each its own event so they ring together */
			static const double chord[3] = {659.26, 783.99, 987.77};
			for (int i = 0; i < 3; i++) {
				event *b = i ? event_new(s, EV_ATMOS + 3, 0, 0.7 * frand(r)) : e;
				if (!b) break;
				if (i) seg_gap(b, 0.12 * i);
				ring(b, chord[i], 3.0, 1.2, -30.0, 0);
			}
			what = "a far chord";
			break;
		}
		default:
			seg_click(e, -32.0);
			seg_gap(e, 0.18);
			seg_click(e, -34.0);
			seg_gap(e, 0.08);
			beep(e, 1174.66, 0, 0.04, -30.0);
			what = "far button clicks";
			break;
		}
		break;
	case SYN_HUM_PHREAK:
		switch (which) {
		case 0:
			seg_add(e, (segment){.kind = SEG_MURMUR, .seconds = 1.5 + 2.5 * chance(r), .amp = db(-38.0), .fade = 0.4});
			what = "crosstalk";
			break;
		case 1:
			seg_add(e, (segment){.kind = SEG_TONE, .f1 = 350, .f2 = 440, .seconds = 0.8 + 1.2 * chance(r), .amp = db(-38.0), .fade = 0.3});
			what = "a far dial tone";
			break;
		case 2:
			seg_add(e, (segment){.kind = SEG_TONE, .f1 = 440, .f2 = 480, .seconds = 2.0, .amp = db(-38.0), .fade = 0.05});
			what = "ringback";
			break;
		case 3: {
			int clicks = 4 + (int)(chance(r) * 9.0);
			for (int i = 0; i < clicks; i++) {
				seg_click(e, -42.0 + 4.0 * frand(r));
				seg_gap(e, 0.02 + 0.1 * chance(r));
			}
			what = "line crackle";
			break;
		}
		case 4:
			seg_click(e, -38.0);
			seg_gap(e, 0.03);
			seg_click(e, -42.0);
			seg_gap(e, 0.08);
			seg_click(e, -40.0);
			what = "a relay";
			break;
		default:
			beep(e, 913.8, 0, 0.33, -36.0);
			beep(e, 1370.6, 0, 0.33, -36.0);
			beep(e, 1776.7, 0, 0.33, -36.0);
			what = "SIT tones";
			break;
		}
		break;
	case SYN_HUM_HACKER:
		switch (which) {
		case 0: {
			int keys = 10 + (int)(chance(r) * 16.0);
			for (int i = 0; i < keys; i++) {
				seg_click(e, -42.0 - 4.0 * chance(r));
				seg_gap(e, 0.085 + 0.03 * frand(r));
			}
			what = "a teletype";
			break;
		}
		case 1:
			seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 100, .f2 = 800, .seconds = 2.5, .amp = db(-40.0), .fade = 1.0});
			what = "a fan spinning up";
			break;
		case 2: {
			int ticks = 15 + (int)(chance(r) * 16.0);
			for (int i = 0; i < ticks; i++) {
				seg_click(e, -42.0);
				seg_gap(e, -0.06 * log(1.0 - 0.95 * chance(r))); /* Geiger: random gaps, 60 ms on average */
			}
			what = "scanning ticks";
			break;
		}
		case 3:
			ring(e, 1200.0, 0.8, 0.25, -36.0, 0);
			seg_gap(e, 0.35);
			ring(e, 1200.0, 0.6, 0.2, -44.0, 0);
			what = "a sonar ping";
			break;
		case 4:
			seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 1000, .f2 = 6000, .seconds = 0.12, .amp = db(-40.0), .fade = 0.01});
			what = "a glitch";
			break;
		default:
			beep(e, 1000.0, 0, 0.15, -40.0);
			what = "a far beep";
			break;
		}
		break;
	case SYN_HUM_DEEP:
		switch (which) {
		case 0:
			seg_add(e, (segment){.kind = SEG_CREAK, .f1 = 120.0, .f2 = 70.0, .seconds = 2.5, .amp = db(-38.0), .fade = 0.6});
			what = "a hull groan";
			break;
		case 1:
			seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 100.0, .f2 = 500.0, .seconds = 3.0, .amp = db(-40.0), .fade = 1.2});
			what = "water rushing past";
			break;
		case 2:
			seg_add(e, (segment){.kind = SEG_SWEEP, .f1 = 60.0, .f2 = 38.0, .seconds = 3.0, .amp = db(-36.0), .fade = 0.05, .decay = 1.2});
			what = "a far rumble";
			break;
		case 3:
			seg_add(e, (segment){.kind = SEG_NOISE, .f1 = 200.0, .f2 = 900.0, .seconds = 2.0, .amp = db(-42.0), .fade = 0.8});
			what = "ballast venting";
			break;
		case 4:
			seg_add(e, (segment){.kind = SEG_TONE, .f1 = 146.83, .f2 = 293.66, .seconds = 1.2, .amp = db(-40.0), .fade = 0.012, .decay = 0.5});
			seg_gap(e, 0.9);
			seg_add(e, (segment){.kind = SEG_TONE, .f1 = 146.83, .f2 = 293.66, .seconds = 1.2, .amp = db(-46.0), .fade = 0.012, .decay = 0.5});
			what = "another boat's ping";
			break;
		default:
			seg_add(e, (segment){.kind = SEG_PAD, .f1 = 55.0, .f2 = 82.41, .seconds = 6.0, .amp = db(-38.0), .fade = 2.5, .cutoff = 300.0, .baud = 0.7});
			what = "the reactor breathing";
			break;
		}
		break;
	case SYN_HUM_TECHNO: {
		/* a dark pad, its chord how hard the machine is working
		 * (moments_tick picks `which` from the total CPU) */
		static const double chords[4][4] = {
			{87.31, 130.81, 174.61, 0.0},    /* at rest: a bare fifth and the octave */
			{87.31, 130.81, 207.65, 311.13}, /* working: F minor 7 */
			{87.31, 130.81, 185.0, 207.65},  /* busy: the flat ninth rubbing the minor third */
			{87.31, 123.47, 185.0, 261.63},  /* saturated: a tritone cluster */
		};
		static const char *const names[4] = {
			"pad: a bare fifth (at rest)", "pad: F minor (working)", "pad: a flat ninth (busy)", "pad: a tritone cluster (saturated)",
		};
		pad(e, chords[which][0], chords[which][1], chords[which][2], chords[which][3], 9.0, -32.0, 450.0 + 150.0 * which);
		what = names[which];
		break;
	}
	case SYN_HUM_BYTEBEAT: {
		/* far off, a few seconds of a classic, wherever t has got to */
		static const struct {
			int formula;
			const char *name;
		} classics[6] = {
			{BB_42, "t*(42&t>>10), the 42 melody"},
			{BB_SIERPINSKI, "t&t>>8, Sierpinski harmony"},
			{BB_VIZNUT, "t*(t>>5|t>>8), viznut's first"},
			{BB_123, "t*(t>>11&t>>8&123&t>>3)"},
			{BB_CHIP, "(t>>6|t|t>>(t>>16))*10+((t>>11)&7)"},
			{BB_63, "t*((t>>12|t>>8)&63&t>>4)"},
		};
		byte_window(e, classics[which].formula, 0, 2.5 + 1.5 * chance(r), -40.0, 0.6, 0.0, 1800.0);
		what = classics[which].name;
		break;
	}
	default:
		if (which == 0) {
			ring(e, F0 * (12 + (int)(chance(r) * 5.0)), 2.5, 0.8, -36.0, 0);
			what = "a far overtone";
		} else {
			pad(e, F0 * 2, F0 * 3, 0, 0, 4.0, -36.0, 900.0);
			what = "a slow swell";
		}
		break;
	}
	say(s, what);
}

/* ---- the synth: steps -> sound --------------------------------------- */

static void synth_init(hum_synth *s, int style, double volume) {
	memset(s, 0, sizeof(*s));
	s->style = style;
	s->volume = volume;
	s->rng = (uint32_t)time(NULL) | 1u;
	s->readout_left = 150; /* the first readout 15 s in */
	s->sweep = 20000.0;
	s->echo_fb = ROOMS[style].echo_feedback;
	s->crush_hold = 1;
	s->crush_mask = 0xFF;
	line_init(&s->line[0], 0);
	line_init(&s->line[1], 7); /* the two calls step their noise at different times */
	/* Freeverb's delay lengths, tuned at 44.1 kHz, the right side 23
	 * samples longer for width */
	static const int comb_tune[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
	static const int allpass_tune[4] = {556, 441, 341, 225};
	for (int side = 0; side < 2; side++) {
		for (int k = 0; k < 8; k++) s->comb_len[side][k] = (comb_tune[k] + 23 * side) * RATE / 44100;
		for (int k = 0; k < 4; k++) s->allpass_len[side][k] = (allpass_tune[k] + 23 * side) * RATE / 44100;
	}
	s->echo_len = (int)(ROOMS[style].echo_seconds * RATE);
}

static void synth_set(hum_synth *s, const hum_steps *steps) {
	s->steps = *steps;
	int n = steps->core_count;
	for (int i = 0; i < n; i++) {
		s->hot[i] = steps->cores[i] >= HOLD_STEP ? s->hot[i] + 1 : 0;
		if (s->hot[i] == HOLD_TICKS) {
			char line[48];
			snprintf(line, sizeof(line), "cpu: core %d pegged for 3 s, holding", i);
			say(s, line);
		}
	}
	s->hot_fsk = steps->fsk >= HOLD_STEP ? s->hot_fsk + 1 : 0;
	s->hot_floor = steps->floor >= 9 ? s->hot_floor + 1 : 0;
	if (s->hot_fsk == HOLD_TICKS) say(s, "network: heavy for 3 s, holding");
	if (s->hot_floor == HOLD_TICKS) say(s, "disk: busy for 3 s, holding");
	if (s->style == SYN_HUM_UPLINK) {
		/* up as traffic passes step 5 (about 30 packets a second), down
		 * once it's been under step 3 for 10 s, so light traffic sitting
		 * on the edge doesn't connect and drop every few seconds */
		if (!s->net_up && steps->fsk >= 5) {
			s->net_up = 1;
			s->net_quiet = 0;
			ev_link(s, 1);
		} else if (s->net_up) {
			s->net_quiet = steps->fsk <= 2 ? s->net_quiet + 1 : 0;
			if (s->net_quiet >= 100) {
				s->net_up = 0;
				ev_link(s, 0);
			}
		}
	}
	/* memory pressure, besides its sound: uplink's tape wobbles, the
	 * harmonic hall shimmers, the phreak's line starts dropping out */
	double stall = steps->stall / 15.0;
	s->wobble = s->style == SYN_HUM_UPLINK ? stall : s->style == SYN_HUM_HARMONIC ? 0.6 * stall : 0.0;
	s->line[0].badness = s->line[1].badness = s->style == SYN_HUM_PHREAK ? 0.5 * stall : 0.0;
	/* techno's DJ closes the filter, from wide open to 400 Hz at the last
	 * step, a quarter of the way there each tick; bytebeat's formulas
	 * lose rate (t held for up to 4 samples) and bits (down to 5) */
	double sweep = steps->stall ? 9000.0 * pow(400.0 / 9000.0, stall) : 20000.0;
	s->sweep += 0.25 * (sweep - s->sweep);
	s->crush_hold = 1 + steps->stall / 5;
	s->crush_mask = (0xFF << (steps->stall / 4)) & 0xFF;
	for (int i = 0; i < MAX_EVENTS; i++) {
		s->events[i].crush_hold = s->crush_hold;
		s->events[i].crush_mask = s->crush_mask;
	}
	/* techno's operations: dust as often as the scheduler switches tasks
	 * (0.4 a second at step 0, a hundred at 15), a spark for processes
	 * starting (none at step 0, a hundred a second at 15). Tasks waiting
	 * for a CPU at step 8 (about 4% of the time) for 3 s bring the
	 * rumble; waiting on IO makes the echo hang, its feedback up from
	 * 0.45 to 0.78 at the last step, a tenth of the way each tick. */
	if (s->style == SYN_HUM_TECHNO) {
		s->dust_rate = 0.4 * pow(1.45, steps->sched);
		s->spark_rate = steps->spawn ? 0.25 * pow(1.5, steps->spawn) : 0.0;
		if (steps->spawn >= 9 && s->spawn_was < 9) say(s, "processes: a burst starting, sparks");
		s->spawn_was = steps->spawn;
		s->hot_wait = steps->cpu_wait >= 8 ? s->hot_wait + 1 : 0;
		if (s->hot_wait == HOLD_TICKS) say(s, "cpu: tasks waiting for a core for 3 s, rumbling");
		double fb = ROOMS[s->style].echo_feedback + 0.33 * steps->io_wait / 15.0;
		s->echo_fb += 0.1 * (fb - s->echo_fb);
		if (steps->io_wait >= 8 && !s->io_said) say(s, "io: tasks waiting on it, the echo hangs");
		s->io_said = steps->io_wait >= 4 && (s->io_said || steps->io_wait >= 8);
	}
}

/* Once a tick, what holds: a pegged core's pad, heavy traffic and a busy
 * disk again as each ends, and memory pressure's sound, faster as it
 * grows: every 2 s at its first step, every 0.2 s at its last */
static void hold_tick(hum_synth *s) {
	if (s->style == SYN_HUM_TECHNO) {
		/* the groove: as many layers as there are pegged cores, up to four */
		int pegged = 0;
		for (int i = 0; i < s->steps.core_count; i++) pegged += s->hot[i] >= HOLD_TICKS;
		for (int layer = 0; layer < pegged && layer < 4; layer++) {
			if (!event_playing(s, EV_HOLD + layer)) techno_layer(s, layer);
		}
		if (s->hot_wait >= HOLD_TICKS && !event_playing(s, EV_RUMBLE)) techno_rumble(s);
	} else {
		for (int i = 0; i < s->steps.core_count; i++) {
			if (s->hot[i] >= HOLD_TICKS && !event_playing(s, EV_HOLD + i)) ev_hold_core(s, i);
		}
	}
	if (s->hot_fsk >= HOLD_TICKS && !event_playing(s, EV_NET)) ev_net(s);
	if (s->hot_floor >= HOLD_TICKS && !event_playing(s, EV_DISK)) ev_disk(s, s->steps.floor * 100 / 15);
	if (s->steps.stall <= 0) {
		s->memory_left = 0;
	} else if (--s->memory_left <= 0) {
		if (s->memory_left == -1) say(s, "memory: under pressure");
		ev_memory(s);
		int every = (int)(20.0 * pow(0.82, s->steps.stall - 1));
		s->memory_left = every < 2 ? 2 : every;
	}
}

/* Once a tick, what happens now and then, and how often */
static void moments_tick(hum_synth *s) {
	const hum_steps *st = &s->steps;
	uint32_t *r = &s->rng;
	int total = 0;
	for (int i = 0; i < st->core_count; i++) {
		double load = st->cores[i] / 15.0;
		total += st->cores[i];
		/* at 30% load a sound every 3-4 s across six cores */
		if (s->hot[i] < HOLD_TICKS && chance(r) < 0.06 * load * load) ev_ping(s, i, st->cores[i]);
	}
	if (s->hot_fsk < HOLD_TICKS && (s->style != SYN_HUM_UPLINK || s->net_up) && chance(r) < 0.0004 * st->fsk * st->fsk) ev_net(s);
	if (s->hot_floor < HOLD_TICKS && chance(r) < 0.0006 * st->floor * st->floor) ev_disk(s, st->floor * 100 / 15);
	if (s->tour) return;
	if (--s->readout_left <= 0) {
		s->readout_left = 450 + (int)(chance(r) * 750.0); /* every 45 s to 2 min */
		ev_readout(s, st->core_count ? total * 100 / (15 * st->core_count) : 0);
	}
	/* about every 45 s for uplink, a minute for the others; half of
	 * uplink's are pad chords (kinds 0 and 5). techno's, about every
	 * 25 s, is a pad whose chord is the machine's whole load. */
	if (s->style == SYN_HUM_TECHNO) {
		if (chance(r) < 1.0 / 250.0) {
			int percent = st->core_count ? total * 100 / (15 * st->core_count) : 0;
			ev_atmos(s, percent < 15 ? 0 : percent < 45 ? 1 : percent < 75 ? 2 : 3);
		}
	} else if (chance(r) < (s->style == SYN_HUM_UPLINK ? 1.0 / 450.0 : 1.0 / 600.0)) {
		int kind = (int)(chance(r) * 6.0);
		if (s->style == SYN_HUM_UPLINK && chance(r) < 0.5) kind = chance(r) < 0.5 ? 0 : 5;
		ev_atmos(s, kind);
	}
}

static float comb_tick(hum_synth *s, int side, int k, float in) {
	float *buf = s->comb[side][k];
	int *pos = &s->comb_pos[side][k];
	float out = buf[*pos];
	float damping = ROOMS[s->style].damping;
	s->comb_store[side][k] = out * (1.0f - damping) + s->comb_store[side][k] * damping;
	buf[*pos] = in + s->comb_store[side][k] * ROOMS[s->style].feedback;
	if (++*pos >= s->comb_len[side][k]) *pos = 0;
	return out;
}

static float allpass_tick(hum_synth *s, int side, int k, float in) {
	float *buf = s->allpass[side][k];
	int *pos = &s->allpass_pos[side][k];
	float held = buf[*pos];
	buf[*pos] = in + held * 0.5f;
	if (++*pos >= s->allpass_len[side][k]) *pos = 0;
	return held - in;
}

/* techno's operations as grains, too small and too many to be events:
 * dust, a tick a few milliseconds long, and sparks, a glint high on the
 * scale bent by its octave */
static void grain_start(hum_synth *s, int spark) {
	grain *g = NULL;
	for (int i = 0; i < GRAINS && !g; i++) {
		if (s->grains[i].left <= 0) g = &s->grains[i];
	}
	if (!g) return;
	uint32_t *r = &s->rng;
	memset(g, 0, sizeof(*g));
	double pan = 0.8 * frand(r), tau;
	g->pan_l = cos((pan + 1.0) * M_PI / 4.0);
	g->pan_r = sin((pan + 1.0) * M_PI / 4.0);
	if (spark) {
		g->inc = TAU * TECHNO_SCALE[(int)(chance(r) * 10.0)] * 4.0 / RATE;
		g->inc2 = 2.0 * g->inc;
		g->index = 1.6;
		g->index_fall = exp(-1.0 / (0.015 * RATE));
		tau = 0.02 + 0.03 * chance(r);
		g->amp = db(-39.0 + 4.0 * frand(r));
	} else {
		g->inc = TAU * (2500.0 + 4500.0 * chance(r)) / RATE;
		tau = 0.002 + 0.004 * chance(r);
		g->amp = db(-41.0 + 5.0 * frand(r));
	}
	g->fall = exp(-1.0 / (tau * RATE));
	g->env = 1.0;
	g->left = (int)(7.0 * tau * RATE); /* gone below -60 dB by then */
}

static void grains_tick(hum_synth *s, double *l, double *r) {
	if (chance(&s->rng) < s->dust_rate / RATE) grain_start(s, 0);
	if (chance(&s->rng) < s->spark_rate / RATE) grain_start(s, 1);
	for (int i = 0; i < GRAINS; i++) {
		grain *g = &s->grains[i];
		if (g->left <= 0) continue;
		g->left--;
		g->ph += g->inc;
		wrap(&g->ph);
		g->ph2 += g->inc2;
		wrap(&g->ph2);
		double v = sin(g->ph + g->index * sin(g->ph2)) * g->env * g->amp;
		g->env *= g->fall;
		g->index *= g->index_fall;
		*l += v * g->pan_l;
		*r += v * g->pan_r;
	}
}

/* The style's room: a tape wobble as deep as `wobble`, an echo that
 * darkens as it repeats, and the reverb. Six seconds after the last sound
 * went in it has all died away below hearing, so it rests till the next. */
static void room(hum_synth *s, double *l, double *r) {
	if (fabs(*l) + fabs(*r) > 1e-6) {
		s->room_quiet = 0;
	} else if (s->room_quiet < RATE * 6) {
		s->room_quiet++;
	} else {
		return;
	}

	if (s->wobble > 0.0) {
		s->wob_l[s->wob_at] = (float)*l;
		s->wob_r[s->wob_at] = (float)*r;
		s->wob_phase += TAU * (0.6 + 2.4 * s->wobble) / RATE;
		wrap(&s->wob_phase);
		double at = s->wob_at - (240.0 + s->wobble * 144.0 * sin(s->wob_phase));
		while (at < 0.0) at += WOB;
		int i0 = (int)at % WOB, i1 = (i0 + 1) % WOB;
		double frac = at - (int)at;
		*l = s->wob_l[i0] * (1.0 - frac) + s->wob_l[i1] * frac;
		*r = s->wob_r[i0] * (1.0 - frac) + s->wob_r[i1] * frac;
		s->wob_at = (s->wob_at + 1) % WOB;
	}

	if (s->echo_len > 0) {
		double fb = s->echo_fb, mix = ROOMS[s->style].echo_mix;
		float el = s->echo_l[s->echo_at], er = s->echo_r[s->echo_at];
		s->echo_dark_l += 0.25 * (er - s->echo_dark_l);
		s->echo_dark_r += 0.25 * (el - s->echo_dark_r);
		s->echo_l[s->echo_at] = (float)(0.5 * (*l + *r) * 0.6 + s->echo_dark_l * fb);
		s->echo_r[s->echo_at] = (float)(s->echo_dark_r * fb);
		s->echo_at = (s->echo_at + 1) % s->echo_len;
		*l += el * mix;
		*r += er * mix;
	}

	float in = (float)((*l + *r) * 0.015);
	float wl = 0.0f, wr = 0.0f;
	for (int k = 0; k < 8; k++) {
		wl += comb_tick(s, 0, k, in);
		wr += comb_tick(s, 1, k, in);
	}
	for (int k = 0; k < 4; k++) {
		wl = allpass_tick(s, 0, k, wl);
		wr = allpass_tick(s, 1, k, wr);
	}
	*l += wl * ROOMS[s->style].wet;
	*r += wr * ROOMS[s->style].wet;
}

static void synth_render(hum_synth *s, int16_t *out, int frames) {
	const int phone = s->style == SYN_HUM_PHREAK;
	const int bytes = phone || s->style == SYN_HUM_BYTEBEAT; /* the phreak's and bytebeat's sounds are made at 8 kHz */
	const double bus = bytes ? LINE_RATE : RATE;
	const int every = bytes ? UPSAMPLE : 1;
	const double lp3800 = 1.0 - exp(-TAU * 3800.0 / RATE); /* takes the edge off 8 kHz, not the grit */
	const double lp200 = 1.0 - exp(-TAU * 200.0 / RATE);
	const double air_norm = 1.0 / sqrt(lp200 / (2.0 - lp200) / 3.0);
	const double attack = 1.0 - exp(-1.0 / (0.005 * RATE)), release = 1.0 - exp(-1.0 / (0.3 * RATE));
	const double ceiling = db(CEILING_DB), limit = db(LIMIT_DB);
	const double lowpass = s->style == SYN_HUM_TECHNO ? s->sweep : ROOMS[s->style].lowpass;
	const double dark = lowpass > 0.0 ? 1.0 - exp(-TAU * lowpass / RATE) : 0.0;
	const double lp120 = 1.0 - exp(-TAU * 120.0 / RATE);
	const double hull_norm = 1.0 / sqrt(lp120 / (2.0 - lp120) / 3.0);
	const double limit_release = 1.0 - exp(-1.0 / (0.06 * RATE));

	for (int f = 0; f < frames; f++) {
		if (s->up == 0) {
			double cl = 0.0, cr = 0.0;
			for (int i = 0; i < MAX_EVENTS; i++) {
				event *e = &s->events[i];
				if (!e->active) continue;
				double v = event_sample(e, &s->rng, bus);
				cl += v * e->pan_l;
				cr += v * e->pan_r;
			}
			if (s->style == SYN_HUM_TECHNO) grains_tick(s, &cl, &cr);
			if (phone) {
				s->held[0] = line_step(&s->line[0], cl, &s->rng);
				s->held[1] = line_step(&s->line[1], cr, &s->rng);
			} else {
				s->held[0] = cl;
				s->held[1] = cr;
			}
		}
		s->up = (s->up + 1) % every;
		s->clock++;
		double l, r;
		if (bytes) {
			s->out[0] += lp3800 * (s->held[0] - s->out[0]);
			s->out[1] += lp3800 * (s->held[1] - s->out[1]);
			l = s->out[0];
			r = s->out[1];
		} else {
			l = s->held[0];
			r = s->held[1];
		}

		if (dark > 0.0) {
			s->dark[0] += dark * (l - s->dark[0]);
			s->dark[1] += dark * (s->dark[0] - s->dark[1]);
			s->dark[2] += dark * (r - s->dark[2]);
			s->dark[3] += dark * (s->dark[2] - s->dark[3]);
			l = s->dark[1];
			r = s->dark[3];
		}

		room(s, &l, &r);

		if (s->style == SYN_HUM_DEEP) {
			/* the hull: a low rumble, breathing very slowly */
			s->hull_lp += lp120 * (frand(&s->rng) - s->hull_lp);
			s->hull_breath += TAU * 0.04 / RATE;
			wrap(&s->hull_breath);
			double hull = s->hull_lp * hull_norm * db(-64.0) * (0.75 + 0.25 * sin(s->hull_breath));
			l += hull;
			r += hull;
		}

		if (s->style == SYN_HUM_HACKER) {
			/* the air: low noise, breathing very slowly */
			s->air_lp += lp200 * (frand(&s->rng) - s->air_lp);
			s->air_breath += TAU * 0.05 / RATE;
			wrap(&s->air_breath);
			double air = s->air_lp * air_norm * db(-68.0) * (0.8 + 0.2 * sin(s->air_breath));
			l += air;
			r += air;
		}

		/* the ceiling: past -22 dBFS everything is pressed down 4:1, so a
		 * pile of sounds under heavy load stays a pile of quiet sounds */
		l *= s->volume;
		r *= s->volume;
		double peak = fmax(fabs(l), fabs(r));
		s->ceiling_env += (peak > s->ceiling_env ? attack : release) * (peak - s->ceiling_env);
		if (s->ceiling_env > ceiling) {
			double g = pow(s->ceiling_env / ceiling, -0.75);
			l *= g;
			r *= g;
		}
		/* the ceiling takes 5 ms to act, and a click is over by then: the
		 * limit acts at once on anything still over -14 dBFS */
		double now = fmax(fabs(l), fabs(r));
		s->limit_env = now > s->limit_env ? now : s->limit_env + limit_release * (now - s->limit_env);
		if (s->limit_env > limit) {
			l *= limit / s->limit_env;
			r *= limit / s->limit_env;
		}
		out[2 * f] = (int16_t)lrint(tanh(l) * 32767.0);
		out[2 * f + 1] = (int16_t)lrint(tanh(r) * 32767.0);
	}
}

/* ---- between the socket and the voice --------------------------------- */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake = PTHREAD_COND_INITIALIZER;
static struct {
	int fresh;            /* a HUM line came since the voice last looked */
	hum_steps steps;      /* the latest line's */
	int style;
	double volume;
	int error, inbound;   /* one-offs, kept until the voice takes them */
	struct timespec last; /* when the last line came */
	int off;              /* HUM-OFF */
	int tour;             /* HUM-TOUR's style, -1 none */
} shared = {.style = SYN_HUM_UPLINK, .volume = 1.0, .tour = -1};

static int clamp_step(int v) {
	return v < 0 ? 0 : v > 15 ? 15 : v;
}

void syn_bar_hum_line(const char *line) {
	hum_steps st;
	memset(&st, 0, sizeof(st));
	int style = -1, error = 0, inbound = 0;
	double volume = -1.0;
	char buf[512];
	snprintf(buf, sizeof(buf), "%s", line);
	char *save = NULL;
	for (char *tok = strtok_r(buf, " \t\r\n", &save); tok; tok = strtok_r(NULL, " \t\r\n", &save)) {
		char *eq = strchr(tok, '=');
		if (!eq) continue; /* "HUM" itself */
		*eq = '\0';
		const char *v = eq + 1;
		if (!strcmp(tok, "style")) {
			style = syn_hum_style_find(v);
		} else if (!strcmp(tok, "volume")) {
			volume = atof(v);
		} else if (!strcmp(tok, "stall")) {
			st.stall = clamp_step(atoi(v));
		} else if (!strcmp(tok, "net")) {
			st.fsk = clamp_step(atoi(v));
		} else if (!strcmp(tok, "disk")) {
			st.floor = clamp_step(atoi(v));
		} else if (!strcmp(tok, "error")) {
			error = atoi(v) > 0;
		} else if (!strcmp(tok, "inbound")) {
			inbound = atoi(v) > 0;
		} else if (!strcmp(tok, "spawn")) {
			st.spawn = clamp_step(atoi(v));
		} else if (!strcmp(tok, "sched")) {
			st.sched = clamp_step(atoi(v));
		} else if (!strcmp(tok, "cpuwait")) {
			st.cpu_wait = clamp_step(atoi(v));
		} else if (!strcmp(tok, "iowait")) {
			st.io_wait = clamp_step(atoi(v));
		} else if (!strcmp(tok, "cores")) {
			for (const char *p = v; *p && st.core_count < MAX_VOICES;) {
				st.cores[st.core_count++] = clamp_step(atoi(p));
				p = strchr(p, ',');
				if (!p) break;
				p++;
			}
		}
	}
	pthread_mutex_lock(&lock);
	shared.steps = st;
	if (style >= 0) shared.style = style;
	if (volume >= 0.0) shared.volume = volume > 4.0 ? 4.0 : volume;
	shared.error |= error;
	shared.inbound |= inbound;
	shared.fresh = 1;
	clock_gettime(CLOCK_MONOTONIC, &shared.last);
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
}

void syn_bar_hum_off(void) {
	pthread_mutex_lock(&lock);
	shared.off = 1;
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
}

void syn_bar_hum_tour(const char *style) {
	char name[32];
	snprintf(name, sizeof(name), "%s", style);
	name[strcspn(name, " \t\r\n")] = '\0';
	int found = syn_hum_style_find(name);
	pthread_mutex_lock(&lock);
	shared.tour = found >= 0 ? found : SYN_HUM_UPLINK;
	shared.off = 0;
	pthread_cond_signal(&wake);
	pthread_mutex_unlock(&lock);
}

static int off_asked(void) {
	pthread_mutex_lock(&lock);
	int off = shared.off;
	pthread_mutex_unlock(&lock);
	return off;
}

/* ---- the voice's thread ---------------------------------------------- */

static pa_simple *stream_open(void) {
	pa_sample_spec ss = {.format = PA_SAMPLE_S16LE, .rate = RATE, .channels = 2};
	pa_buffer_attr ba = {(uint32_t)-1, (uint32_t)pa_usec_to_bytes(100000, &ss), (uint32_t)-1, (uint32_t)-1, (uint32_t)-1};
	int err;
	pa_simple *pa = pa_simple_new(NULL, "syn-bar-core", PA_STREAM_PLAYBACK, NULL, "acoustic monitor", &ss, NULL, &ba, &err);
	if (!pa) fprintf(stderr, "syn-bar-core: acoustic monitor: no audio (%s)\n", pa_strerror(err));
	return pa;
}

/* Plays some frames; a stream that's gone (the server restarted) is
 * closed, and the next block opens another */
static void play(pa_simple **pa, const int16_t *buf, int frames) {
	int err;
	if (pa_simple_write(*pa, buf, (size_t)frames * 4, &err) < 0) {
		pa_simple_free(*pa);
		*pa = NULL;
	}
}

/* A last tenth of a second with nothing new starting, so stopping fades
 * instead of cutting, then the stream closes */
static void fade_out(hum_synth *synth, pa_simple **pa, int16_t *buf) {
	memset(synth->events, 0, sizeof(synth->events));
	synth_render(synth, buf, TICK_FRAMES);
	play(pa, buf, TICK_FRAMES);
	if (*pa) {
		int err;
		pa_simple_drain(*pa, &err);
		pa_simple_free(*pa);
		*pa = NULL;
	}
}

/* The tour: the same sections syn-hum test narrates (SYN_HUM_TOUR), each
 * scripted here. HUM-OFF stops it. */
static void run_tour(hum_synth *s, pa_simple **pa, int16_t *buf) {
	long n = sysconf(_SC_NPROCESSORS_ONLN);
	int cores = n < 1 ? 1 : n > MAX_VOICES ? MAX_VOICES : (int)n;
	s->tour = 1;
	for (int sec = 0; sec < SYN_HUM_TOUR_SECTIONS; sec++) {
		if (syn_hum_tour_skips(s->style, sec)) continue;
		int ticks = (int)(SYN_HUM_TOUR[sec].seconds * 10);
		for (int t = 0; t < ticks; t++) {
			if (!*pa || off_asked()) return;
			hum_steps st;
			memset(&st, 0, sizeof(st));
			st.core_count = cores;
			for (int i = 0; i < cores; i++) st.cores[i] = 1;
			st.spawn = 2; /* a quiet desktop: a process every second or so, */
			st.sched = 4; /* a few thousand context switches a second */
			int moments = 0;
			switch (sec) {
			case 0:
				synth_set(s, &st);
				break;
			case 1: {
				int every = s->style == SYN_HUM_TECHNO ? 40 : 26; /* techno's four pads, a little apart */
				synth_set(s, &st);
				if (t % every == 5 && t / every < SYN_HUM_STYLES[s->style].atmos_kinds) ev_atmos(s, t / every);
				break;
			}
			case 2: {
				int phase = t / 30;
				static const int steps[3] = {5, 8, 10};
				static const int every[3] = {14, 6, 3};
				for (int i = 0; i < cores; i++) st.cores[i] = steps[phase];
				synth_set(s, &st);
				if (t % every[phase] == 0) ev_ping(s, (t / every[phase]) * 7 % cores, steps[phase]);
				break;
			}
			case 3:
				st.cores[cores > 3 ? 3 : cores - 1] = 15;
				synth_set(s, &st);
				break;
			case 4:
				synth_set(s, &st);
				if (t == 5) ev_readout(s, 47);
				break;
			case 5:
				st.fsk = t >= 30 ? 13 : 5;
				synth_set(s, &st);
				if (t == 3 || t == 25) ev_net(s);
				break;
			case 6:
				st.floor = t >= 40 ? 13 : 2;
				synth_set(s, &st);
				if (t == 3) ev_disk(s, 3);
				if (t == 20) ev_disk(s, 25);
				break;
			case 7:
				st.stall = 13;
				synth_set(s, &st);
				if (t == 3) ev_readout(s, 88);
				if (t % 6 == 0) ev_ping(s, t / 6 % cores, 9);
				break;
			case 8:
				synth_set(s, &st);
				if (t == 3) ev_error(s);
				break;
			case 9:
				synth_set(s, &st);
				if (t == 3) ev_inbound(s);
				break;
			case 10: {
				/* processes starting, the scheduler busier, in three steps */
				static const int spawn[3] = {3, 8, 13}, sched[3] = {4, 9, 14};
				st.spawn = spawn[t / 40];
				st.sched = sched[t / 40];
				synth_set(s, &st);
				break;
			}
			case 11:
				/* tasks waiting for a CPU, then on IO as well, a few pings
				 * to hear the echo hang */
				st.cpu_wait = 11;
				st.io_wait = t >= 50 ? 13 : 0;
				synth_set(s, &st);
				if (t % 8 == 2) ev_ping(s, t / 8 % cores, 8);
				break;
			case 12: {
				static const int busy[] = {12, 15, 7, 14, 9, 13, 10, 15};
				for (int i = 0; i < cores; i++) st.cores[i] = busy[i % 8];
				st.fsk = 9;
				st.floor = 7;
				st.stall = 2;
				st.spawn = 9;
				st.sched = 11;
				st.cpu_wait = 9;
				st.io_wait = 5;
				synth_set(s, &st);
				if (t == 40) ev_error(s);
				moments = 1;
				break;
			}
			default:
				synth_set(s, &st);
				moments = 1;
				break;
			}
			hold_tick(s);
			if (moments) moments_tick(s);
			synth_render(s, buf, TICK_FRAMES);
			play(pa, buf, TICK_FRAMES);
		}
	}
}

static void *voice(void *arg) {
	(void)arg;
	static hum_synth synth;
	static int16_t buf[TICK_FRAMES * 2];
	pa_simple *pa = NULL;
	int style = -1;
	long block = 0;
	for (;;) {
		pthread_mutex_lock(&lock);
		while (!pa && !shared.fresh && shared.tour < 0) {
			pthread_cond_wait(&wake, &lock);
		}
		int tour = shared.tour;
		shared.tour = -1;
		int off = shared.off;
		shared.off = 0;
		hum_steps st = shared.steps;
		int error = shared.error, inbound = shared.inbound;
		shared.error = shared.inbound = 0;
		int want = shared.style;
		double volume = shared.volume;
		shared.fresh = 0;
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		double quiet = (double)(now.tv_sec - shared.last.tv_sec) + (double)(now.tv_nsec - shared.last.tv_nsec) / 1e9;
		pthread_mutex_unlock(&lock);

		if (tour >= 0) {
			if (!pa) pa = stream_open();
			if (pa) {
				synth_init(&synth, tour, volume);
				run_tour(&synth, &pa, buf);
				if (pa) fade_out(&synth, &pa, buf);
			}
			style = -1;
			continue;
		}
		/* syn-hum said stop, or stopped saying anything */
		if (off || quiet > QUIET_SECONDS) {
			if (pa) fade_out(&synth, &pa, buf);
			continue;
		}
		if (!pa) {
			pa = stream_open();
			if (!pa) {
				sleep(2); /* no audio server yet: try again, without spinning */
				continue;
			}
			style = -1;
			block = 0;
		}
		if (want != style) {
			synth_init(&synth, want, volume);
			style = want;
		}
		synth.volume = volume;
		/* The levels move a tick (five blocks) at a time, so everything
		 * counted in ticks stays in tenths of a second; an error or a new
		 * connection plays on the very next block */
		if (block++ % BLOCKS_PER_TICK == 0) {
			synth_set(&synth, &st);
			hold_tick(&synth);
			moments_tick(&synth);
		}
		if (error) ev_error(&synth);
		if (inbound) ev_inbound(&synth);
		synth_render(&synth, buf, BLOCK_FRAMES);
		play(&pa, buf, BLOCK_FRAMES); /* blocks until the server wants more: this is what paces the voice */
	}
	return NULL;
}

int syn_bar_hum_start(void) {
	pthread_t thread;
	if (pthread_create(&thread, NULL, voice, NULL) != 0) {
		return -1;
	}
	pthread_detach(thread);
	return 0;
}
