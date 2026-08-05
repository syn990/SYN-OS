/* ------------------------------------------------------------------------
 *   Single source of truth for every path this tool touches — replaces
 *   the old WORKDIR/ISO_OUTPUT/.syncache split BUILD-ARCHISO.zsh
 *   accumulated ad hoc across its own history. One root, six
 *   subdirectories, anchored at a fixed XDG data location independent of
 *   wherever the SYN-OS checkout happens to live (see syn_repo_locate.h
 *   for THAT resolution — build state and the source checkout are
 *   deliberately unrelated now, so cloning/moving/deleting the repo
 *   never drags gigabytes of scratch/output/ISOs with it), handed to
 *   every other module from here rather than each file constructing its
 *   own paths.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_BUILD_PATHS_H
#define SYN_BUILD_PATHS_H

#include <stddef.h>
#include <stdbool.h>

typedef struct {
	char root[1024];       /* ~/.local/share/syn-os/iso-builder */
	char sources[1024];    /* .../sources — bare git mirrors */
	char scratch[1024];    /* .../scratch — mkarchiso -w target, wiped per build */
	char extracted[1024];  /* .../extracted — ad hoc commit extractions */
	char output[1024];     /* .../output — mkarchiso -o target, wiped per build */
	char isos[1024];       /* .../isos — permanent, finished ISOs + build logs */
} syn_build_paths;

/* Fills every field, anchored at ~/.local/share/syn-os/iso-builder —
 * fixed, independent of the resolved repo checkout. Does not create any
 * directories; callers create what they need when they need it (mkdir
 * -p semantics), matching how syn_git_extract.c already behaves. */
void syn_build_paths_init(syn_build_paths *out);

/* This tool runs as root (see main.c's re-exec under doas) — plain
 * getenv("HOME") at that point is root's home, not the invoking user's.
 * Resolves the real invoking user's home via $DOAS_USER/$SUDO_USER +
 * getpwnam() instead, falling back to $HOME if neither is set (e.g.
 * genuinely logged in as root). Anything that used to read $HOME
 * directly for a real user's config/theme/browse-start-dir should call
 * this instead. */
const char *syn_resolve_real_home(void);

/* Takes an exclusive flock() on a lockfile under paths->root, held for
 * the life of the process (released automatically on exit/crash — no
 * separate cleanup needed, unlike a plain pidfile that can go stale).
 * Returns true if the lock was acquired, false if another instance
 * already holds it — confirmed live: two instances launched against
 * the same fixed scratch path raced on the SAME cmake build
 * directories mid-build, one instance's files disappearing/changing
 * under the other's feet, producing "missing CMakeCache.txt" failures
 * that looked like a build-loop bug but were actually just two
 * processes fighting over one scratch tree. Call once, at startup,
 * before anything touches paths->scratch. */
bool syn_build_paths_lock(const syn_build_paths *paths);

#endif
