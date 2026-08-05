/* ------------------------------------------------------------------------
 *   Builds every SYN-SOFTWARE tool (each a "*-src" directory) for the
 *   target profile's live environment: cmake configure, build, then
 *   DESTDIR-install into
 *   <profile>/airootfs. Direct C port of BUILD-ARCHISO.zsh's own loop
 *   (added and hardened earlier this session specifically because
 *   failures used to be swallowed silently — per-tool logs are
 *   preserved here for the same reason, not a regression). Entirely
 *   unprivileged — none of this needs root.
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_SOFTWARE_BUILD_H
#define SYN_SOFTWARE_BUILD_H

#include <stdbool.h>

typedef void (*syn_software_build_line_cb)(const char *line, void *userdata);

/* `software_dir` is <repo_root>/SYN-SOFTWARE. `build_root` is where each
 * tool's actual cmake build directory lands (pass paths->scratch) — NOT
 * a sibling of the source directory inside software_dir itself, which
 * used to dump a real <tool>-src-build/ folder directly into the
 * tracked repo tree on every build. `profile_airootfs` is
 * <profile>/airootfs — each tool's cmake --install DESTDIR target.
 * `log_dir` is where per-tool build logs land (caller-owned directory,
 * must already exist) — matches the log-preservation behavior added to
 * BUILD-ARCHISO.zsh earlier this session, so a build failure is always
 * diagnosable after the fact, not just during this one run. `on_line`
 * (may be NULL) is called for each tool's status line (start/success/
 * failure headline only, not full per-tool cmake output — that stays in
 * the per-tool log file). Returns true even if some tools failed
 * (matches the original script's "continue without it" behavior — a
 * failed tool just won't be on the resulting ISO) — only returns false
 * on something that prevents the loop from running at all (e.g.
 * software_dir doesn't exist). */
bool syn_software_build_all(const char *software_dir, const char *build_root,
	const char *profile_airootfs, const char *log_dir,
	syn_software_build_line_cb on_line, void *userdata);

#endif
