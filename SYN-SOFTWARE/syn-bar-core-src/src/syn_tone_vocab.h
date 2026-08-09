/* ------------------------------------------------------------------------
 *   OS-wide tone meanings, shared by every tool that plays a cue via
 *   syn_bar_tone_play()/syn_bar_tone_play_dtmf() (see syn_bar_tone.h) —
 *   one place to look up "what does a 1900Hz tone mean" instead of each
 *   tool re-deriving its own frequency pairs and duplicating the
 *   meaning-to-Hz mapping. A given meaning sounds the same everywhere:
 *   SUCCESS always means the same thing whether it's a build finishing
 *   or a VPN connecting.
 *
 *   Frequencies below the DTMF grid (see syn-uplink-dialpad-src's own
 *   keys[] table for the real 4x4 grid this avoids colliding with) are
 *   picked to be either a real single frequency already established by
 *   syn-bar-core's relay tones (1900/2600Hz), or an otherwise-unclaimed
 *   DTMF row/col pair, so a new tool adopting this table can't
 *   accidentally alias an existing meaning.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-TONE-VOCAB
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TONE_VOCAB_H
#define SYN_TONE_VOCAB_H

/* A clean action completed: connected, encrypted/decrypted, built,
 * launched. Single tone, not a DTMF pair — matches syn-bar-core's
 * original RELAY_CONNECT_TONE_HZ. */
#define SYN_TONE_SUCCESS_HZ 1900.0
#define SYN_TONE_SUCCESS_SECONDS 0.2

/* An action failed: wrong password, unreachable host, operation
 * rejected. Matches syn-bar-core's original RELAY_FAIL_DTMF. */
#define SYN_TONE_FAIL_LOW 697.0
#define SYN_TONE_FAIL_HIGH 1209.0
#define SYN_TONE_FAIL_SECONDS 0.15

/* A clean, intentional stop: disconnect, stop watching, stop hosting —
 * distinct from FAIL because nothing went wrong, the user just ended
 * something. Matches syn-bar-core's original RELAY_DISCONNECT_DTMF. */
#define SYN_TONE_STOP_LOW 941.0
#define SYN_TONE_STOP_HIGH 1336.0
#define SYN_TONE_STOP_SECONDS 0.15

/* A binary state flipped ON (muted, enabled, armed) — distinct from
 * SUCCESS because this isn't "an operation completed," it's "a
 * persistent state changed," and the two directions of a toggle should
 * sound different from each other so audio alone tells you which way
 * it went. */
#define SYN_TONE_TOGGLE_ON_LOW 852.0
#define SYN_TONE_TOGGLE_ON_HIGH 1209.0
#define SYN_TONE_TOGGLE_OFF_LOW 852.0
#define SYN_TONE_TOGGLE_OFF_HIGH 1336.0
#define SYN_TONE_TOGGLE_SECONDS 0.1

/* Something needs attention without the user having done anything —
 * a background condition crossed a threshold (e.g. thermal). Distinct
 * from INTRUSION_DTMF (syn-bar-core's own, reserved for "another
 * machine is polling this one") — this is a local hardware/system
 * condition, not a network security event. Callers MUST edge-detect
 * (fire once when the condition becomes true, not once per poll/redraw
 * while it stays true) — see syn-bar-core's own was_fresh/was_watching
 * static-bool pattern in main.c for the shape to copy. */
#define SYN_TONE_ALERT_LOW 941.0
#define SYN_TONE_ALERT_HIGH 1209.0
#define SYN_TONE_ALERT_SECONDS 0.2

#endif
