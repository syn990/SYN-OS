/* ------------------------------------------------------------------------
 *   The crew's bed: who is doing what, from the file SYN-CASTER (the
 *   crew's radio bot) rewrites every few seconds and the moment anyone
 *   starts or stops talking, $XDG_RUNTIME_DIR/syn-crew.state:
 *
 *     # a comment
 *     talk 1                 someone is talking on the channel (0: nobody)
 *     crew william2 working 4   a member, what they're doing (idle,
 *     crew blackmesa waiting 4  waiting, working or talking) and their
 *     crew jackal idle 0        ticket's priority, P1-P5 (0 none)
 *
 *   Anything else can write it too; syn-hum only reads it. A file older
 *   than 30 s is a bot that stopped, and is taken as nobody.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_HUM_CREW_H
#define SYN_HUM_CREW_H

#include "syn_hum_conf.h"

#include <stddef.h>
#include <time.h>

#define SYN_HUM_CREW_MAX 8  /* the voice gives eight members a bed */
#define SYN_HUM_CREW_STALE 30

typedef struct {
	int count, talk;
	struct {
		char id[16];
		char state;   /* i, h (waiting: on hold), w or t */
		int priority; /* 0-5 */
	} member[SYN_HUM_CREW_MAX];
	struct timespec seen; /* the file's mtime when it was read */
} syn_hum_crew;

/* Reads the file again if it changed since `c` was filled, leaving out
 * the members `conf` mutes, and nobody with crew off. 1 if `c` changed. */
int syn_hum_crew_read(syn_hum_crew *c, const syn_hum_conf *conf);

/* The HUM line's " talk=N crew=ID:SP,..." for it, appended at `out` */
int syn_hum_crew_keys(const syn_hum_crew *c, char *out, size_t len);

/* In words, for syn-hum.state: "william2 working P4, overwatch waiting P3" */
void syn_hum_crew_words(const syn_hum_crew *c, char *out, size_t len);

#endif
