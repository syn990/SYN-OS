/* ------------------------------------------------------------------------
 *   Commit-history browsing against a bare mirror clone, cached under
 *   paths->sources (see syn_build_paths.h — a fixed XDG data location,
 *   NOT inside the SYN-OS repo checkout; these mirrors are full bare
 *   clones of real project history and have no business living in a
 *   source tree git status walks). The caller can point this at any git
 *   URL (untested/unsupported territory beyond the two built-in repos,
 *   per explicit user decision: "they can simply go into an option in
 *   the menu and change the repo URL, untested that's on them kinda
 *   terrain, we won't care too much for it for now"), cached under its
 *   own directory derived from the URL.
 *
 *   No libgit2 (confirmed zero prior use anywhere in this codebase;
 *   every git interaction anywhere in this repo, including the retired
 *   BUILD-ARCHISO.zsh, shells out to the git CLI). Commands run via
 *   fork()+execvp(), never system()/popen(), matching
 *   syn-relay-src/syn-connect-src's documented convention of
 *   avoiding a shell for a fixed argv.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_GIT_HISTORY_H
#define SYN_GIT_HISTORY_H

#include <stdbool.h>
#include <stddef.h>

#define SYN_GIT_MAIN_URL "https://github.com/syn990/SYN-OS.git"
#define SYN_GIT_RTOS_URL "https://github.com/syn990/SYN-RTOS.git"

typedef struct {
	char short_sha[16];
	char full_sha[48];
	char date[16];   /* YYYY-MM-DD */
	char author[64];
	char subject[256];
} syn_git_commit;

/* Derives the mirror directory for `remote_url` into `out`, under
 * `sources_dir` (pass paths->sources). The two built-in URLs above map
 * onto fixed `syn-os.git`/`syn-rtos.git` names so browsing SYN-OS/RTOS
 * history never creates a second redundant clone; any other URL gets
 * its own directory under `sources_dir/custom/`, keyed by a hash of the
 * URL. */
void syn_git_mirror_path(char *out, size_t out_len, const char *sources_dir, const char *remote_url);

/* Ensures the mirror for `remote_url` exists and is up to date: `git
 * clone --mirror` if absent, `git --git-dir=... fetch --quiet origin
 * "+refs/heads/(all):refs/heads/(all)"` otherwise. `sources_dir` is
 * paths->sources — used only to anchor where mirrors are cached,
 * unrelated to which repo is being browsed. Returns false on a hard
 * failure (e.g. clone failed and no cached mirror exists to fall back
 * to). */
bool syn_git_ensure_mirror(const char *sources_dir, const char *remote_url);

/* Lists commits reachable from the mirror's default branch, newest
 * first, into `out` (caller-allocated, `max` entries). Returns the
 * count actually filled, or -1 on error (mirror missing/unreadable). */
int syn_git_list_commits(const char *sources_dir, const char *remote_url,
	syn_git_commit *out, int max);

#endif
