/* ------------------------------------------------------------------------
 *   Resolves where the SYN-OS checkout this tool operates on actually
 *   lives — replaces the hardcoded ~/GithubProjects/SYN-OS assumption
 *   that both syn-build-launcher.zsh and this session's own first-pass
 *   menu.xml wiring baked in with no real basis (not XDG, not a git
 *   convention, just whatever directory name happened to get used
 *   originally). Three-step resolution, cheapest/most-common case first:
 *
 *     1. Auto-detect: cwd (or an ancestor) already looks like a real
 *        SYN-OS checkout (SYN-ISO-PROFILE/profiledef.sh AND
 *        SYN-SOFTWARE/ both present — two markers, not one, so a
 *        random directory that happens to contain a stray profiledef.sh
 *        doesn't false-positive). No config, no prompt — matches how a
 *        developer actually uses this tool, already sitting inside one.
 *     2. Config file: ~/.config/syn-os/iso-builder.conf's repo_path=
 *        line, if step 1 didn't find anything (same manual key=value
 *        parsing style syn_theme.c already uses for its own config).
 *     3. First-run prompt: ask via syn_tui_text_prompt, default
 *        suggestion ~/.local/share/syn-os/SYN-OS (a real XDG data
 *        location), clone there if empty, save the answer to the config
 *        file so this never asks again.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_REPO_LOCATE_H
#define SYN_REPO_LOCATE_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
	SYN_REPO_SOURCE_AUTODETECTED,
	SYN_REPO_SOURCE_CONFIG,
	SYN_REPO_SOURCE_PROMPTED,
} syn_repo_source;

/* Resolves the repo root via the three-step order above, filling
 * `out` (absolute path, no trailing slash) and `source_out` (which
 * step actually resolved it, for the dashboard to disclose — see
 * syn_tui_dashboard.h). May run the interactive prompt (step 3), so
 * must be called with ncurses already initialized. Returns false only
 * if the prompt was cancelled or the resulting path still doesn't
 * contain a valid checkout after an attempted clone. */
bool syn_repo_locate_resolve(char *out, size_t out_len, syn_repo_source *source_out);

#endif
