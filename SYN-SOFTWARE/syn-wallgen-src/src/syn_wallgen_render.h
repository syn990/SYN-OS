/* ------------------------------------------------------------------------
 *   Renders one 1920x1080 wallpaper for a theme: a radial base gradient,
 *   a SYN_THEME_FAMILY-specific pattern overlay (hairline grid / slab
 *   bands / halo rings / bevel sheen — Vanilla gets none), a soft glow,
 *   and a vignette. Every call re-jitters light position and pattern
 *   density from syn_wg_rng, so the same theme renders a little
 *   differently each run — see syn_wallgen_rng.h.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WALLGEN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_WALLGEN_RENDER_H
#define SYN_WALLGEN_RENDER_H

#include "syn_wallgen_theme.h"

#define SYN_WG_W 1920
#define SYN_WG_H 1080

typedef struct {
	syn_wg_rgb *px; /* row-major, SYN_WG_W * SYN_WG_H */
} syn_wg_image;

void syn_wg_image_free(syn_wg_image *im);

/* mode: "dark" or "light" (SYN_THEME_MODE). family: SYN_THEME_FAMILY,
 * e.g. "SYN-OS-HALO" — an unrecognized/absent family renders as plain
 * Vanilla (gradient + glow + vignette, no pattern overlay). Caller owns
 * the returned image and must free it with syn_wg_image_free(). */
syn_wg_image syn_wg_build_wallpaper(const char *mode, const char *family,
                                     syn_wg_rgb bg_alt, syn_wg_rgb accent,
                                     syn_wg_rgb accent_dim, syn_wg_rgb border);

#endif
