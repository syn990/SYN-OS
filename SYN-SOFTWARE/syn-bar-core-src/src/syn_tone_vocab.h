/* ------------------------------------------------------------------------
 *   OS-wide tone meanings. syn-bar-core is the ONLY thing in SYN-OS that
 *   knows any of these frequencies or ever calls paplay (see
 *   syn_bar_tone.c) — every other tool asks for a MEANING by name over
 *   syn-bar-core's socket (see syn_bar_notify.h) and has no idea what
 *   Hz that turns into. Retuning what FAIL sounds like, or adding a new
 *   meaning, is a one-file edit here — no other binary needs touching or
 *   rebuilding, since none of them link this table or syn_bar_tone.c at
 *   all anymore.
 *
 *   Frequencies below the DTMF grid (see syn-uplink-dialpad-src's own
 *   keys[] table for the real 4x4 grid this avoids colliding with) are
 *   picked to be either a real single frequency already established by
 *   syn-bar-core's relay tones (1900/2600Hz), or an otherwise-unclaimed
 *   DTMF row/col pair, so a new meaning here can't accidentally alias
 *   an existing one.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-CORE (tone vocabulary)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TONE_VOCAB_H
#define SYN_TONE_VOCAB_H

#include <string.h>

typedef struct {
	const char *name;   /* wire name, sent verbatim after "TONE-MEANING " */
	double hz1, hz2;     /* hz2 <= 0 means a single tone, not DTMF-style dual */
	double seconds;
} syn_tone_meaning;

/* SUCCESS: a clean action completed — connected, encrypted/decrypted,
 *   built, launched. Single tone (matches syn-bar-core's original
 *   RELAY_CONNECT_TONE_HZ).
 * FAIL: an action failed — wrong password, unreachable host, operation
 *   rejected (matches the original RELAY_FAIL_DTMF).
 * STOP: a clean, intentional stop — disconnect, stop watching, stop
 *   hosting. Distinct from FAIL because nothing went wrong (matches the
 *   original RELAY_DISCONNECT_DTMF).
 * TOGGLE_ON / TOGGLE_OFF: a persistent binary state flipped (muted,
 *   enabled, armed) — distinct from SUCCESS because this isn't "an
 *   operation completed," it's "a state changed," and the two
 *   directions should sound different from each other.
 * ALERT: something needs attention without the user having done
 *   anything — a background condition crossed a threshold (e.g.
 *   thermal). Distinct from syn-bar-core's own INTRUSION tone (reserved
 *   for "another machine is polling this one," a network security
 *   event, not a local hardware one). Callers MUST edge-detect this
 *   themselves (ask once when the condition becomes true, not once per
 *   poll/redraw while it stays true) — see syn-bar-core's own
 *   was_fresh/was_watching static-bool pattern in main.c for the shape
 *   to copy; this table has no memory of what a caller last asked for.
 * KEY_CLICK: one character typed/deleted in a masked password/login
 *   field (syn-uplink-dialpad's --password/--login/--exec screens). ONE
 *   fixed pair regardless of which character was typed — a per-key
 *   DTMF tone here would leak the password's length and timing over
 *   audio, same risk landline phones avoid by not tone-dialing while
 *   entering a PIN. Pitched between the real DTMF grid's rows/columns
 *   (697-941 low, 1209-1633 high — see syn-uplink-dialpad-src's own
 *   keys[] table) so it can't be mistaken for an actual dialed digit.
 * START: a long-running background operation began (e.g. an ISO
 *   build) — distinct from SUCCESS/FAIL since nothing has concluded
 *   yet, just started.
 * STREAM_FAIL: a syn-relay --stream-app launch failed specifically —
 *   kept distinct from plain FAIL (same "digit D" pair syn-bar-core's
 *   AGENT_WATCHING already uses) since a successful stream has no
 *   tone of its own (the app's window simply appearing already
 *   confirms it), so a failure needs to sound different enough from
 *   every other FAIL to be worth a dedicated meaning. */
static const syn_tone_meaning SYN_TONE_TABLE[] = {
	{"SUCCESS",     1900.0, -1.0,   0.2},
	{"FAIL",         697.0, 1209.0, 0.15},
	{"STOP",         941.0, 1336.0, 0.15},
	{"TOGGLE_ON",    852.0, 1209.0, 0.1},
	{"TOGGLE_OFF",   852.0, 1336.0, 0.1},
	{"ALERT",        941.0, 1209.0, 0.2},
	{"KEY_CLICK",   1050.0, 1400.0, 0.05},
	{"START",        697.0, 1477.0, 0.15},
	{"STREAM_FAIL",  941.0, 1633.0, 0.15},
};
#define SYN_TONE_TABLE_COUNT (sizeof(SYN_TONE_TABLE) / sizeof(SYN_TONE_TABLE[0]))

/* Looks up `name` (case-sensitive, exact match) in the table above.
 * Returns NULL if it's not a recognized meaning — callers should treat
 * that as silently doing nothing, same as any other malformed/unknown
 * request on this socket. */
static inline const syn_tone_meaning *syn_tone_vocab_lookup(const char *name) {
	for (size_t i = 0; i < SYN_TONE_TABLE_COUNT; i++) {
		if (strcmp(SYN_TONE_TABLE[i].name, name) == 0) {
			return &SYN_TONE_TABLE[i];
		}
	}
	return NULL;
}

#endif
