/* ------------------------------------------------------------------------
 *   The acoustic monitor's voice: syn-hum samples the machine and sends
 *   its levels here ten times a second as HUM lines; this turns them into
 *   sound in the style they name (syn_hum_styles.h), on a thread of its
 *   own with one PulseAudio stream, open only while lines keep coming.
 *   syn-bar-core stays the only thing in SYN-OS that plays sound.
 *
 *   The socket verbs (main.c hands them over):
 *
 *     HUM style=phreak volume=1.00 stall=0 net=3 disk=0 error=0 inbound=0
 *         spawn=2 sched=5 cpuwait=3 iowait=0 cores=1,0,2   (one line)
 *         the levels, each a step 0-15; error and inbound are one-offs.
 *         spawn, sched, cpuwait and iowait (processes started, context
 *         switches, waiting for a CPU, on IO) are only techno's so far;
 *         a key the voice doesn't know is passed over, and one missing
 *         is 0, so either side can be older than the other.
 *         Two seconds without one and the voice fades out by itself, so a
 *         syn-hum that died doesn't leave the machine humming.
 *     HUM-OFF          fade out now
 *     HUM-TOUR NAME    every sound of a style in turn (syn-hum test)
 *     HUM-HEARD        replies with the last few sounds, newest first,
 *                      one "HH:MM:SS what" per line (the bar's tooltip)
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-BAR-CORE (acoustic monitor)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_BAR_HUM_H
#define SYN_BAR_HUM_H

/* Starts the voice's thread, silent until the first HUM line. -1 if the
 * thread couldn't start; the rest of syn-bar-core carries on without it. */
int syn_bar_hum_start(void);

void syn_bar_hum_line(const char *line);
void syn_bar_hum_off(void);
void syn_bar_hum_tour(const char *style);
void syn_bar_hum_heard(int fd);

#endif
