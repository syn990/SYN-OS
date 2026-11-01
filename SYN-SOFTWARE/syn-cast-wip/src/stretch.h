/* ------------------------------------------------------------------------
 *   Replay speed without the chipmunk: WSOLA time-stretching for 48 kHz
 *   mono speech. The phone app replays history at 1x, 1.25x and 1.5x
 *   (HistoryPlayerSpeed, through SoundTouch); this does the same in a few
 *   hundred lines of C.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-CAST
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_CAST_STRETCH_H
#define SYN_CAST_STRETCH_H

#include <stddef.h>
#include <stdint.h>

typedef struct stretch stretch;

stretch *stretch_new(void);
void stretch_free(stretch *s);
void stretch_reset(stretch *s, double speed);
/* Feeds n samples; writes up to cap processed samples, returns how many. */
size_t stretch_process(stretch *s, const int16_t *in, size_t n, int16_t *out, size_t cap);
/* Emits what is left once the input has ended. */
size_t stretch_flush(stretch *s, int16_t *out, size_t cap);

#endif
