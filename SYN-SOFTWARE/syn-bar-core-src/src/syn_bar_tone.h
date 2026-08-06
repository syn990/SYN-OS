/* ------------------------------------------------------------------------
 *   Plays a short sine-wave tone via a raw PCM pipe into paplay.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-TONE
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_BAR_TONE_H
#define SYN_BAR_TONE_H

/* Forks and returns immediately — does not wait for playback. */
void syn_bar_tone_play(double hz, double seconds);

#endif
