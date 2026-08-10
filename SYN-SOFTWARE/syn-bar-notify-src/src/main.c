/* ------------------------------------------------------------------------
 *                       S Y N - B A R - N O T I F Y
 *
 *   Thin CLI wrapper around syn_bar_notify_meaning() (see
 *   syn-bar-core-src/src/syn_bar_notify.c) for shell scripts — anything
 *   that wants a SYN-OS tone cue but isn't already a native C tool
 *   (e.g. a doas-pacman wrapper) shells out to this instead of
 *   reimplementing the Unix-socket write itself. Same fire-and-forget
 *   contract as the C API: exits 0 whether or not syn-bar-core was even
 *   running to hear it.
 *
 *   Usage:
 *     syn-bar-notify <MEANING>
 *
 *   Where <MEANING> is one of syn_tone_vocab.h's names (SUCCESS, FAIL,
 *   STOP, TOGGLE_ON, TOGGLE_OFF, ALERT) — this binary has no idea what
 *   any of them sound like, only syn-bar-core does.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-NOTIFY (CLI)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#include "syn_bar_notify.h"

#include <stdio.h>

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "usage: %s <MEANING>\n", argv[0]);
		fprintf(stderr, "  MEANING: see syn-bar-core-src/src/syn_tone_vocab.h's SYN_TONE_TABLE\n");
		fprintf(stderr, "           for the current list (SUCCESS, FAIL, STOP, ...) — this\n");
		fprintf(stderr, "           binary has no copy of it, only the bar does.\n");
		return 1;
	}
	syn_bar_notify_meaning(argv[1]);
	return 0;
}
