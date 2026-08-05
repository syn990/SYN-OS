/* ------------------------------------------------------------------------
 *   Plays a short sine-wave tone through the default PulseAudio/PipeWire
 *   sink, via a raw PCM pipe into paplay — no new heavy audio dependency
 *   (like syn-audio's full libpulse use) for a single short beep, no temp
 *   file. Reused by syn-relay-src for its own connection-event tones —
 *   see syn-audio-src/syn-wifi-src's existing cross-package sibling-.c
 *   reuse convention (CMakeLists.txt pulling syn_theme.c straight from
 *   syn-crypter-src) for the precedent this follows.
 *
 *   Non-blocking: forks and returns immediately, so a short poll-driven
 *   process (waybar re-execs this kind of tool every few seconds) isn't
 *   held open waiting for playback to finish.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-TONE
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_BAR_TONE_H
#define SYN_BAR_TONE_H

/* Forks, generates `seconds` of a `hz` sine wave as signed 16-bit mono
 * PCM at 44100Hz with a short fade-in/out (avoids the click a
 * hard-edged burst would have), and pipes it into paplay. Returns
 * immediately in the caller — does not wait for playback. */
void syn_bar_tone_play(double hz, double seconds);

#endif
