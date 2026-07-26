/* ------------------------------------------------------------------------
 *   xorshift128+ seeded from /dev/urandom — not for anything security
 *   sensitive, just fast, decent-quality jitter so two installs' (or two
 *   dev runs') wallpapers for the same theme aren't byte-identical.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WALLGEN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_WALLGEN_RNG_H
#define SYN_WALLGEN_RNG_H

/* Must be called once before any syn_wg_rng_* call. Falls back to a fixed
 * seed if /dev/urandom can't be read (never expected in practice, but
 * this must not crash a Stage 1 install over it). */
void syn_wg_rng_seed(void);

double syn_wg_rng_uniform(double lo, double hi);
int syn_wg_rng_randint(int lo, int hi); /* inclusive on both ends */

#endif
