/* ------------------------------------------------------------------------
 *   Asks syn-bar-core to play a named tone meaning, rather than
 *   synthesizing audio here — syn-bar-core is the ONLY component in
 *   SYN-OS that talks to PulseAudio (see its syn_bar_tone.c) or knows
 *   any tone's actual frequency (see its syn_tone_vocab.h). Every other
 *   tool that wants a sound sends the meaning's name over the bar's
 *   existing Unix socket and moves on immediately, the same way
 *   everything else that wants the bar to react already works
 *   (RELAY-STATUS/AGENT-WATCHING/etc. verbs). Fire-and-forget — no
 *   reply is read, a missing/unreachable syn-bar-core (or an
 *   unrecognized meaning name) is silently a no-op, matching every
 *   other best-effort notify in this codebase (e.g. notify-send calls).
 *
 *   This is the ONLY tone API any tool other than syn-bar-core itself
 *   should ever call — no tool links syn_bar_tone.c/.h directly except
 *   syn-bar-core, so adding or retuning a sound is a one-file edit to
 *   syn_tone_vocab.h, never a rebuild of every caller.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-NOTIFY
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_BAR_NOTIFY_H
#define SYN_BAR_NOTIFY_H

/* Sends "TONE-MEANING <name>" to syn-bar-core's socket — `name` must
 * match one of syn_tone_vocab.h's SYN_TONE_TABLE entries, e.g.
 * "SUCCESS", "FAIL", "STOP", "TOGGLE_ON", "TOGGLE_OFF", "ALERT". */
void syn_bar_notify_meaning(const char *name);

/* Sends "TONE-RAW <hz1> <hz2> <seconds>" — the one escape hatch from
 * the named-meaning table above, for the one caller (syn-uplink-dialpad)
 * whose entire job is playing an arbitrary, real-time, user-chosen
 * frequency (an actual dialed DTMF digit, or its own reference keypad
 * of syn-bar-core's other tones) rather than a fixed status cue. hz2<=0
 * means a single tone, matching syn_bar_tone_play()'s own contract.
 * Nothing else in SYN-OS should call this — if you're tempted to, you
 * probably want syn_bar_notify_meaning() and a new entry in
 * syn_tone_vocab.h instead. */
void syn_bar_notify_raw(double hz1, double hz2, double seconds);

#endif
