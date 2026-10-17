/* ------------------------------------------------------------------------
 *   ~/.config/syn-os/hum.conf: the acoustic monitor's style and volume,
 *   one "key value" a line. Written by `syn-hum set`, which the menu's
 *   Acoustic Monitor entries run; read when syn-hum starts and again on
 *   SIGHUP, which `syn-hum set` sends a running one.
 *
 *     style uplink       uplink, phreak, hacker, harmonic, deep, techno or bytebeat (syn_hum_styles.h)
 *     volume normal      quiet, normal or loud
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-HUM (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_HUM_CONF_H
#define SYN_HUM_CONF_H

typedef struct {
	int style;          /* SYN_HUM_PHREAK ... */
	char volume[16];    /* "quiet", "normal" or "loud" */
} syn_hum_conf;

/* Defaults for anything missing or unreadable: uplink, normal */
void syn_hum_conf_load(syn_hum_conf *c);

/* Writes the whole file in one rename. 0, or -1 with errno set. */
int syn_hum_conf_save(const syn_hum_conf *c);

/* The volume word as a gain: quiet 0.5, normal 1, loud 2. -1 for a word
 * that isn't one of them. */
double syn_hum_volume_gain(const char *word);

#endif
