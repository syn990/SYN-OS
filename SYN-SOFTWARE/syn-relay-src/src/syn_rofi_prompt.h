/* ------------------------------------------------------------------------
 *   Shared rofi text-prompt helper — replaces what used to be three
 *   near-identical .zsh wrapper scripts (syn-relay-connect-prompt.zsh,
 *   syn-agent-watch-prompt.zsh, syn-agent-host-prompt.zsh) whose only
 *   job was "ask rofi for an IP, hand it to a binary." Since this is
 *   now one binary, it asks rofi itself when a role flag (--connect,
 *   --watch, --host-watched) is given no argument — menu.xml calls the
 *   flag directly, no intermediary script.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-RELAY
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_ROFI_PROMPT_H
#define SYN_ROFI_PROMPT_H

#include <stdbool.h>
#include <stddef.h>

/* Pops a themed rofi input box (same palette/style as
 * syn-picker-lib.zsh's syn_pick::rofi) with the given prompt text.
 * Copies the typed line into `out` (up to out_size, NUL-terminated,
 * trailing newline stripped). Returns false if rofi isn't available,
 * the popup was dismissed, or nothing was typed — callers should treat
 * false as "user cancelled," not an error to report. */
bool syn_rofi_prompt(const char *prompt, char *out, size_t out_size);

#endif
