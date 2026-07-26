/* ------------------------------------------------------------------------
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-WALLGEN (Desktop)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_WALLGEN_PNG_H
#define SYN_WALLGEN_PNG_H

#include "syn_wallgen_render.h"

/* Writes im as an 8-bit RGB PNG to path. Returns 0 on success, -1 on any
 * failure (path not writable, libpng error). */
int syn_wg_write_png(const char *path, const syn_wg_image *im);

#endif
