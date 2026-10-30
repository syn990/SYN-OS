/* ------------------------------------------------------------------------
 *   The acoustic monitor's styles, in words: each one's name, what it's
 *   like, and what every signal sounds like in it. syn-bar-core plays
 *   them (syn_bar_hum.c); syn-hum lists them (`syn-hum styles`) and
 *   narrates the tour (`syn-hum test`), so the words live here, once,
 *   next to the sounds they describe.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-CORE (acoustic monitor)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_HUM_STYLES_H
#define SYN_HUM_STYLES_H

#include <string.h>

enum {
	SYN_HUM_UPLINK, SYN_HUM_PHREAK, SYN_HUM_HACKER, SYN_HUM_HARMONIC, SYN_HUM_DEEP, SYN_HUM_TECHNO, SYN_HUM_BYTEBEAT,
	SYN_HUM_STYLE_COUNT
};

/* For messages: every style's name, the way they're listed */
#define SYN_HUM_STYLE_LIST "uplink, phreak, hacker, harmonic, deep, techno or bytebeat"

enum {
	SYN_HUM_OUT_IDLE, SYN_HUM_OUT_CPU, SYN_HUM_OUT_CPU_HELD, SYN_HUM_OUT_READOUT,
	SYN_HUM_OUT_NET, SYN_HUM_OUT_NET_HELD, SYN_HUM_OUT_DISK, SYN_HUM_OUT_DISK_HELD,
	SYN_HUM_OUT_MEMORY, SYN_HUM_OUT_ERROR, SYN_HUM_OUT_INBOUND, SYN_HUM_OUT_ATMOS,
	SYN_HUM_OUT_OPS, SYN_HUM_OUT_WAIT, SYN_HUM_OUT_CREW,
	SYN_HUM_OUT_COUNT
};

static const char *const SYN_HUM_OUT_NAMES[SYN_HUM_OUT_COUNT] = {
	"idle", "cpu", "cpu, held 3 s", "readout", "network", "network, heavy",
	"disk", "disk, heavy", "memory", "log error", "inbound", "atmosphere",
	"operations", "waiting", "crew",
};

/* A style that doesn't play a signal leaves its line NULL: `syn-hum
 * styles` leaves it out and the tour skips it. Operations (processes
 * started, context switches), waiting (CPU and IO pressure) and the crew
 * (who of SYN-CASTER's crew is doing what) are only techno's so far. */
static const struct {
	const char *name, *summary;
	int atmos_kinds;
	const char *out[SYN_HUM_OUT_COUNT];
} SYN_HUM_STYLES[SYN_HUM_STYLE_COUNT] = {
	{"uplink", "Uplink at 3 am: the game's interface tones over its music, slow pads and a long room", 6, {
		"silence and the room's reverb; now and then a slow pad chord swells and fades",
		"the password breaker: fast ticks as characters cycle, then a lock tone at the core's note; more often the harder it works",
		"the core's note swells into a slow pad and keeps breathing",
		"the CPU % as bounce beeps rising, one per 10%: 47% is five",
		"connecting: bounce beeps and a two-tone 'connected' when traffic starts, 'disconnected' when it stops, data blips between",
		"an arpeggio runs under the beeps, sixteenths into the echo",
		"the file copier: a quick rising run of blips",
		"copy after copy",
		"the trace tracker: beeps coming faster as memory stalls, and the sound wobbling like stretched tape",
		"two falling beeps: access denied",
		"three rising beeps and a ping: an incoming connection",
		"slow pad chords, a far sweep, a deep swell, a glass chord, far button clicks",
	}},
	{"phreak", "a phone line heard in a small room: soft MF tones, modem scraps, a switchboard ticking over", 6, {
		"an open digital phone line, 8 kHz mu-law, almost silent",
		"the core's digit in soft MF tones, more often the harder it works: a switchboard ticking over",
		"a dial tone that swells and fades, breathing",
		"the CPU % in MF, as a blue box sent digits: KP 4 7 ST",
		"soft scraps of modem: Bell 202 bursts, answer tones, Bell 103 handshakes",
		"modem scraps one after another",
		"a relay picking up",
		"relay after relay",
		"bursts of the busy signal, coming faster as memory stalls; the line drops out more",
		"reorder: the fast busy signal, twice",
		"a burst of ringback: someone's calling",
		"crosstalk, a far dial tone, ringback, line crackle, a relay, the SIT tones",
	}},
	{"hacker", "a dark, precise system in a tight room: ticks, glints and data shimmer in E phrygian", 6, {
		"server-room air, low and almost silent",
		"a precise tick and a glass glint at the core's note, more often the harder it works",
		"a dark minor third that swells and fades, breathing",
		"the CPU % in binary as glass tones, high for 1 and low for 0: 47 is 0101111",
		"data shimmer: a breath of high noise and a few soft pips",
		"shimmer after shimmer",
		"a low pulse",
		"pulse after pulse",
		"sonar pings, coming faster as memory stalls",
		"two low notes falling: denied",
		"a sonar sweep and its ping",
		"a teletype, a fan spinning up, scanning ticks, a sonar ping, a glitch, a far beep",
	}},
	{"harmonic", "the harmonic series in a hall: soft harp notes, each core on its own harmonic of 110 Hz", 2, {
		"silence",
		"a harp note on the core's harmonic, more often the harder it works",
		"the core's harmonic as a pad that swells and fades, breathing",
		"the CPU % as an arpeggio up the series, a note per 10%",
		"sparkles high on the series, the 11th to the 16th harmonic",
		"sparkles one after another",
		"the fundamental and its octave, swelling and fading",
		"swell after swell",
		"two notes 3 Hz apart, beating, coming faster as memory stalls; the hall shimmers",
		"falling notes, 16th to 12th harmonic",
		"rising notes, 8th to 10th harmonic",
		"now and then a single high overtone like a far bell, or a slow swell",
	}},
	{"deep", "inside a submarine: low sonar, the engine, the hull, water; nothing bright", 6, {
		"near silence: a faint low rumble of the hull",
		"a low sonar ping on the core's own note, more often the harder it works",
		"the engine: a low throb that breathes",
		"the CPU % as depth soundings: a short low ping per 10%",
		"distant propeller wash, beating at the blade rate",
		"the propeller keeps turning",
		"the hull creaking",
		"creak after creak",
		"pressure groans, deeper and more often as memory stalls",
		"a low klaxon pulse, twice",
		"contact: a ping, and its return",
		"a hull groan, water rushing past, a far rumble, ballast venting, another boat's ping, the reactor breathing",
	}},
	{"techno", "dark techno at 128 bpm in F phrygian: metal, rumble and static on a sixteenth-note grid, the ambience made of what the system is doing", 4, {
		"dust: faint ticks as often as the scheduler switches tasks, a few a second on a quiet desktop, and the echo's tail",
		"struck metal at the core's note, on the next sixteenth; brighter the harder it works",
		"the groove, a layer per pegged core, each coming in on the bar: the kick, a rolling bass, hats, a clank on two and four",
		"the CPU % as low metal tolls on the eighths, one per 10%: 47% is five",
		"bursts of static on the sixteenths",
		"static gated on every sixteenth for a bar, the off-beats harder",
		"a machine clank",
		"clank after clank",
		"a dissonant stab into the echo, faster as memory stalls, and the whole mix's filter closing",
		"an impact: a low boom and a metal scrape",
		"a swell, and a high ping ringing into the echo",
		"a dark pad, its chord set by total CPU: a bare fifth at rest, minor when working, a flat ninth when busy, a tritone cluster when saturated",
		"sparks: a glint for each process started, a cloud of them during a build; the dust thickens as context switches climb",
		"tasks waiting for a CPU bring a rumble under the beat; tasks waiting on IO make the echo hang longer",
		"a bar of plucks per crew member at work, each on their own note and rhythm, louder and busier the higher their ticket's priority; a soft bell every other bar for one waiting; an octave up for the one talking, while the whole hum ducks 10 dB",
	}},
	{"bytebeat", "the machine as one-line formulas: windows onto t*(42&t>>10) and its kin, 8-bit at 8 kHz", 6, {
		"silence",
		"a window onto the core's own melody, t*((t>>10&M)|4), each core a different M; more often the harder it works",
		"the core's melody playing on, breathing",
		"the CPU % as rising saws, t*8, t*10, t*12..., one per 10%: 47% is five",
		"a glitch of Sierpinski harmony, t&t>>8",
		"(t*5&t>>7)|(t*3&t>>10), running",
		"a crunch of t*(t>>5|t>>8)",
		"crunch after crunch",
		"a chirp, t*t>>6, coming faster as memory stalls, and every formula losing bits and rate",
		"two saws falling a fifth: t*12, then t*8",
		"three saws rising, t*16, t*20, t*24: a major triad",
		"far off: the 42 melody, Sierpinski, viznut's first, t*(t>>11&t>>8&123&t>>3), a chiptune, t*((t>>12|t>>8)&63&t>>4)",
	}},
};

/* The tour (`syn-hum test`): every sound of a style in turn. Each
 * section plays one line of the style's list, and the held version part
 * way through where there is one; the last two are the whole machine. */
static const struct {
	double seconds;
	int out;           /* SYN_HUM_OUT_*, or -1 for a label of its own */
	int then;          /* the held version, part way through; -1 none */
	const char *label;
} SYN_HUM_TOUR[] = {
	{4, SYN_HUM_OUT_IDLE, -1, NULL},
	{16, SYN_HUM_OUT_ATMOS, -1, NULL},
	{9, SYN_HUM_OUT_CPU, -1, "cpu at 30%, 50%, 70%"},
	{7, SYN_HUM_OUT_CPU_HELD, -1, "one core pegged"},
	{4, SYN_HUM_OUT_READOUT, -1, "47%"},
	{9, SYN_HUM_OUT_NET, SYN_HUM_OUT_NET_HELD, NULL},
	{9, SYN_HUM_OUT_DISK, SYN_HUM_OUT_DISK_HELD, NULL},
	{7, SYN_HUM_OUT_MEMORY, -1, NULL},
	{2.5, SYN_HUM_OUT_ERROR, -1, NULL},
	{2.5, SYN_HUM_OUT_INBOUND, -1, NULL},
	{12, SYN_HUM_OUT_OPS, -1, "processes starting, the scheduler getting busier"},
	{10, SYN_HUM_OUT_WAIT, -1, "tasks waiting for a CPU, then on IO"},
	{16, SYN_HUM_OUT_CREW, -1, "one member working, a second on a P1, a third waiting, then the first talking"},
	{8, -1, -1, "a busy machine"},
	{4, -1, -1, "back to idle"},
};
#define SYN_HUM_TOUR_SECTIONS (int)(sizeof(SYN_HUM_TOUR) / sizeof(SYN_HUM_TOUR[0]))

/* A section of the tour about a signal the style doesn't play is left out,
 * by syn-hum's narration and syn-bar-core's playing alike */
static inline int syn_hum_tour_skips(int style, int section) {
	int out = SYN_HUM_TOUR[section].out;
	return out >= 0 && SYN_HUM_STYLES[style].out[out] == NULL;
}

/* A style's number from its name, or -1 */
static inline int syn_hum_style_find(const char *name) {
	for (int i = 0; i < SYN_HUM_STYLE_COUNT; i++) {
		if (strcmp(name, SYN_HUM_STYLES[i].name) == 0) {
			return i;
		}
	}
	return -1;
}

#endif
