/* ------------------------------------------------------------------------
 *   The persistent home screen — Target/Repo/Scratch/Output/Launch/Quit
 *   all on one screen, at once, for the whole session. Owns picking the
 *   build target (opens a focused sub-picker per source type — commit
 *   browser, named-build list, a path/URL prompt — then returns to THIS
 *   same screen with the target updated) and launching the build.
 *   Replaces both the old flat top-level `syn_tui_menu` loop and the
 *   old separate "confirm dashboard" screen shown only after a flow
 *   already ran — direct user complaint about that two-stage shape:
 *   "nested as fuck menus... I wanted way more integrated main menu
 *   that has the launch button." Named after (and modeled on)
 *   syn-crypter-src's own syn_tui_dashboard — a persistent status
 *   screen instead of sequential blind menus is already this
 *   codebase's own precedent.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_TUI_DASHBOARD_H
#define SYN_TUI_DASHBOARD_H

#include "syn_build_runner.h"
#include "syn_build_paths.h"
#include "syn_repo_locate.h"

/* Runs the home screen for the whole session: renders Target/Repo
 * checkout/Scratch/Output/Launch/Quit, handles all navigation
 * (including opening and returning from the Target sub-pickers),
 * launches a build on confirm, and only returns once the user quits.
 * `paths` MAY BE REWRITTEN internally (the Scratch row toggles
 * paths->scratch between disk/tmpfs) — this is fine since nothing
 * outside this call reads `paths` while it's running. `repo_root` and
 * `repo_source` are needed for the Repo-checkout disclosure line and
 * for resolving git-mirror/extraction paths when a Target sub-picker
 * needs them. */
void syn_tui_dashboard_run(syn_build_paths *paths, const char *repo_root,
	syn_repo_source repo_source);

#endif
