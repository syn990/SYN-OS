/* ------------------------------------------------------------------------
 *   Extracts a full commit tree via `git archive | tar -x` (same
 *   mechanism BUILD-ARCHISO.zsh's --build= path already uses for a
 *   known profile_path), then locates profiledef.sh within it by
 *   searching rather than requiring a hardcoded path — arbitrary
 *   commits picked from live history don't have a build-manifest.json
 *   entry telling us where the profile directory lives, and that path
 *   has moved around SYN-OS's own history already (see
 *   build-manifest.json's varying profile_path values across entries:
 *   "releng", "SYN-OS-V4/SYN-ISO-PROFILE", etc).
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_GIT_EXTRACT_H
#define SYN_GIT_EXTRACT_H

#include <stdbool.h>
#include <stddef.h>

/* Extracts the full tree at `commit_sha` from the mirror at
 * `mirror_path` into a fresh scratch directory under
 * `<extracted_dir>/<short_sha>/` (pass paths->extracted — see
 * syn_build_paths.h — never a path inside the repo checkout itself),
 * then searches (bounded depth, first match wins) for a profiledef.sh
 * and returns the directory containing it in `profile_dir_out`. Returns
 * false if extraction fails or no profiledef.sh is found anywhere in
 * the tree; `err_out` (if non-NULL) is filled with a human-readable
 * reason (e.g. "permission denied creating <path>", "git archive
 * failed", "no profiledef.sh found in extracted tree") instead of the
 * caller only learning that *something* failed. */
bool syn_git_extract_and_find_profile(const char *extracted_dir, const char *mirror_path,
	const char *commit_sha, char *profile_dir_out, size_t profile_dir_out_len,
	char *err_out, size_t err_out_len);

#endif
