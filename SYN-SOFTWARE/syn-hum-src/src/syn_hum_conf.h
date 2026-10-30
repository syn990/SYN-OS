/* ------------------------------------------------------------------------
 *   ~/.config/syn-os/hum.conf: the acoustic monitor's style and volume,
 *   one "key value" a line. Written by `syn-hum set`, which the menu's
 *   Acoustic Monitor entries run; read when syn-hum starts and again on
 *   SIGHUP, which `syn-hum set` sends a running one.
 *
 *     style uplink       uplink, phreak, hacker, harmonic, deep, techno or bytebeat (syn_hum_styles.h)
 *     volume normal      quiet, normal or loud
 *     crew on            on or off: the crew's bed, from SYN-CASTER's
 *                        $XDG_RUNTIME_DIR/syn-crew.state (syn_hum_crew.h)
 *     mute jackal        one member left out of it, a line each
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_HUM_CONF_H
#define SYN_HUM_CONF_H

#define SYN_HUM_MUTE_MAX 16

typedef struct {
	int style;          /* SYN_HUM_PHREAK ... */
	char volume[16];    /* "quiet", "normal" or "loud" */
	int crew;           /* 1: the crew's bed plays */
	char mute[SYN_HUM_MUTE_MAX][16]; /* members left out of it */
	int mute_count;
} syn_hum_conf;

/* Defaults for anything missing or unreadable: uplink, normal, crew on,
 * nobody muted */
void syn_hum_conf_load(syn_hum_conf *c);

/* Writes the whole file in one rename. 0, or -1 with errno set. */
int syn_hum_conf_save(const syn_hum_conf *c);

/* The volume word as a gain: quiet 0.5, normal 1, loud 2. -1 for a word
 * that isn't one of them. */
double syn_hum_volume_gain(const char *word);

/* 1 if hum.conf mutes this member */
int syn_hum_conf_muted(const syn_hum_conf *c, const char *id);

/* Mutes a member (on 0) or brings them back (on 1); 0, or -1 when the
 * list is full */
int syn_hum_conf_mute(syn_hum_conf *c, const char *id, int on);

#endif
