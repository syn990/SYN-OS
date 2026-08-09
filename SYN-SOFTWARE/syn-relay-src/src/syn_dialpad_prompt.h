/* ------------------------------------------------------------------------
 *   Pops syn-uplink-dialpad, reads back whatever it printed on Enter.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_DIALPAD_PROMPT_H
#define SYN_DIALPAD_PROMPT_H

#include <stdbool.h>
#include <stddef.h>

/* `prompt` is unused — the dialpad's window title is static. Returns
 * false (out untouched past an empty string) on q/Esc/unavailable —
 * treat as "cancelled," not an error. */
bool syn_dialpad_prompt(const char *prompt, char *out, size_t out_size);

#endif
