/* ------------------------------------------------------------------------
 *   The real build orchestrator — everything BUILD-ARCHISO.zsh used to
 *   do, now native. Runs entirely as root (main.c re-execs under doas at
 *   startup if not already root), same as BUILD-ARCHISO.zsh's whole run
 *   always has — an earlier version split privilege (unprivileged
 *   profile prep/SYN-SOFTWARE build loop, doas only for mkarchiso) to
 *   shrink the privileged surface, but every step touching scratch/ then
 *   had to guess whether a prior run's root-owned leftovers made it
 *   privileged or not, which was a real, recurring source of bugs
 *   (stale-ownership wipe failures, log corruption from a build step
 *   racing a permission failure) a single root process can't have.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_BUILD_RUNNER_H
#define SYN_BUILD_RUNNER_H

#include "syn_build_paths.h"

#include <stdbool.h>

/* Which resolve_*() flow produced a target — lets the dashboard render
 * real per-source-type detail (SHA/author/date for a commit) instead of
 * just the flattened target_label, without the dashboard needing to
 * know each resolver's internals. Three kinds, matching the tool's
 * three real target choices (current tree / a commit / a local
 * profile.sh directory). */
typedef enum {
	SYN_TARGET_CURRENT_TREE,
	SYN_TARGET_COMMIT,
	SYN_TARGET_LOCAL_PROFILE,
} syn_target_kind;

/* Everything needed to run one build — filled by whichever resolve_*()
 * function in syn_tui_dashboard.c resolved a target (current tree /
 * browsed commit / local profile dir), then held by the home screen for
 * display and, on Launch, passed to syn_build_run(). The kind-specific
 * fields below are only valid for their matching `kind` — e.g.
 * `commit.short_sha` is meaningless when `kind != SYN_TARGET_COMMIT`.
 * They exist so the dashboard can show real structured detail per
 * source type instead of collapsing everything into `target_label`
 * alone (that field stays, used for the short one-line summary shown
 * while picking and for log messages). */
typedef struct {
	char profile_dir[1024];   /* resolved profile directory (has profiledef.sh) */
	char build_id[160];       /* names the output ISO — e.g. "local-2026-08-03",
	                            * or "<short-sha>-<date>" for a browsed commit */
	char target_label[256];   /* human-readable one-line summary, e.g.
	                            * "Current working tree" or a commit subject */

	syn_target_kind kind;
	union {
		struct {
			char short_sha[16];
			char subject[256];
			char author[64];
			char date[16];
		} commit;
		struct {
			char path[1024];
		} local_profile;
		struct {
			char repo_root[1024];
		} current_tree;
	} detail;
} syn_build_target;

typedef void (*syn_build_line_cb)(const char *line, void *userdata);

/* Called roughly every 200ms during the mkarchiso step whenever there's
 * been no new output line to redraw from (see run_mkarchiso()) — lets
 * the caller's on-screen panel pick up a terminal resize even during a
 * quiet stretch (e.g. a slow pacman download producing no output for a
 * while), instead of the panel staying at its old size until the next
 * line arrives. May be NULL if the caller doesn't need this (e.g. the
 * plain-stdout fallback path, which redraws nothing). */
typedef void (*syn_build_idle_cb)(void *userdata);

/* Called on every \r-redraw of a live progress bar (mksquashfs,
 * pacman's download bars) with the current in-progress line — the
 * caller is expected to update ONE line in place (see
 * syn_tui_buildlog_update_provisional()), not append a new permanent
 * line each time. An earlier version tried periodic on_line snapshots
 * of the \r-buffered text instead, which flooded the scrollback with
 * dozens of near-duplicate lines (confirmed live: "15356/67687"
 * repeated four times in a row) — this replaces that entirely. May be
 * NULL if the caller doesn't need live progress display. */
typedef void (*syn_build_provisional_cb)(const char *line, void *userdata);

/* Runs the full pipeline for `target` against `paths` (all of it as
 * root — see main.c):
 *   1. wipe+recreate paths->scratch and paths->output
 *   2. mount cleanup on paths->scratch (stray binds from a crashed
 *      prior run)
 *   3. SYN-SOFTWARE build loop into target->profile_dir/airootfs
 *   4. strip the retired [community] repo from the profile's
 *      pacman.conf if present
 *   5. ensure grub-install is present if the profile declares a grub
 *      bootmode
 *   6. mkarchiso -v -w <scratch> -o <output> <profile_dir>
 *   7. on success, move the resulting ISO to out_dir/<out_name>.iso and
 *      archive the per-tool build logs alongside it
 *
 * `on_line` receives every line of output from every step (SYN-SOFTWARE
 * build loop included) for a single unified scrolling log, same as the
 * old zsh script's terminal output was one continuous stream. `on_idle`
 * and `on_provisional` (both may be NULL) are described above.
 * `repo_root` is the resolved SYN-OS checkout (from
 * syn_repo_locate_resolve()) — used to find SYN-SOFTWARE/ for step 3,
 * since it's unrelated to `paths` (build state) and can't be derived
 * from target->profile_dir (which may point at an extracted commit or
 * an arbitrary local profile dir with no SYN-SOFTWARE/ sibling).
 * `out_dir`/`out_name` are the user's chosen output location (dashboard
 * defaults these to paths->isos/target->build_id, but both are
 * editable via the Output row — nothing here assumes they still match
 * paths->isos or target->build_id). Returns true only if mkarchiso
 * itself succeeded (a SYN-SOFTWARE tool failing does not fail the whole
 * build, matching the original script's behavior). `final_iso_path_out`
 * (must be at least 1200 bytes) is filled with the finished ISO's real
 * path on success. */
bool syn_build_run(const syn_build_target *target, const syn_build_paths *paths,
	const char *repo_root, const char *out_dir, const char *out_name,
	syn_build_line_cb on_line, syn_build_idle_cb on_idle,
	syn_build_provisional_cb on_provisional, void *userdata, char *final_iso_path_out);

#endif
