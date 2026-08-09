/* ------------------------------------------------------------------------
 *   Asks syn-bar-core to play a tone, rather than synthesizing audio
 *   here — syn-bar-core is the one component in SYN-OS that talks to
 *   PulseAudio (see its syn_bar_tone.c); everything else that wants a
 *   sound asks the bar to make it over its existing Unix socket, the
 *   same way everything else that wants the bar to react already works
 *   (RELAY-STATUS/AGENT-WATCHING/etc. verbs). Fire-and-forget — no
 *   reply is read, a missing/unreachable syn-bar-core is silently a
 *   no-op (matching every other best-effort notify in this codebase,
 *   e.g. cmd_stats_client.c's notify-send calls).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY (stats client role)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_BAR_NOTIFY_H
#define SYN_BAR_NOTIFY_H

/* Sends "TONE-DTMF <hz1> <hz2> <seconds>" to syn-bar-core's socket. */
void syn_bar_notify_tone_dtmf(double hz1, double hz2, double seconds);

/* Sends "TONE <hz> <seconds>". */
void syn_bar_notify_tone(double hz, double seconds);

#endif
