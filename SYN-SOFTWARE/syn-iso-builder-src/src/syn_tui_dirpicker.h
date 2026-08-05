/* ------------------------------------------------------------------------
 *   A real, browsable Miller-column directory picker — ported from
 *   syn-crypter-src's proven syn_tui_file_picker (parent/current/
 *   preview columns, arrow-key navigate, Backspace up a level) instead
 *   of the blind "type a path and hope" text prompt this tool used to
 *   fall back on. Direct user complaint about that gap: "cannot select
 *   the dirs or anything" / wanted a real browsable picker, not a raw
 *   text field. Adapted for picking a DIRECTORY rather than a file —
 *   Enter descends into a directory; a distinct key selects the
 *   currently-open directory itself, since that's what this tool
 *   actually needs.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TUI_DIRPICKER_H
#define SYN_TUI_DIRPICKER_H

#include <stddef.h>

/* Browsable directory picker starting at `start_dir`. Arrow keys/j-k
 * move within the current column, Enter descends into the highlighted
 * directory, Backspace goes up a level, 's' selects the CURRENTLY OPEN
 * directory (not a highlighted entry — there is no "select this file"
 * concept here, only "select this directory"), Esc/q cancels. Returns
 * 0 and fills `out` with the selected directory's path, or -1 on
 * cancel. `marker_filename` (may be NULL) is checked for in every
 * listed directory — when present, matching directories get a `*`
 * hint in the listing, and the currently-open directory gets an
 * explicit "<marker_filename> found here" callout. Pass "profiledef.sh"
 * when browsing for a build profile; pass NULL when there's no
 * particular file to hint at (e.g. picking a plain output directory). */
int syn_tui_dirpicker(const char *title, const char *start_dir, const char *marker_filename, char *out, size_t out_len);

#endif
