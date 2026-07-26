/* ------------------------------------------------------------------------
 *   Reads a .theme file's SYN_* key/value lines (mode, family, name, and
 *   hex palette) — the fuller field set syn-wallgen needs, distinct from
 *   syn-crypter-src's syn_theme.c which only loads a fixed 8-color palette
 *   for ncurses color pairs.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WALLGEN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_WALLGEN_THEME_H
#define SYN_WALLGEN_THEME_H

#include <stddef.h>

typedef struct { double r, g, b; } syn_wg_rgb; /* 0..255 */

#define SYN_WG_MAX_KV 32

typedef struct {
	char key[64];
	char val[128];
} syn_wg_kv;

typedef struct {
	syn_wg_kv kv[SYN_WG_MAX_KV];
	int n;
} syn_wg_theme_vals;

/* Parses every SYN_KEY="value" line in path into out. Returns -1 if the
 * file can't be opened, 0 otherwise (a file with no matching lines still
 * returns 0 with out->n == 0 — callers fall back via syn_wg_theme_get). */
int syn_wg_parse_theme(const char *path, syn_wg_theme_vals *out);

/* Looks up key, returning def if not present. The returned pointer is
 * either def or a pointer into *t — valid as long as *t is. */
const char *syn_wg_theme_get(const syn_wg_theme_vals *t, const char *key, const char *def);

/* Parses a "#rrggbb" (or "rrggbb") string into 0..255 RGB components. */
syn_wg_rgb syn_wg_hexrgb(const char *hex);

#endif
