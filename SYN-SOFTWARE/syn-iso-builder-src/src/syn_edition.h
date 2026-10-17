/* ------------------------------------------------------------------------
 *   Editions (SYN-EDITIONS/NAME.conf) — the repo's own declarative build
 *   files, one per way of building SYN-OS, read the one way they're
 *   meant to be read: through BUILD.zsh, never parsed here. An edition is
 *   plain zsh (WorkDir="$HOME/..."), so the only reader that gets every
 *   value right is zsh itself; `BUILD.zsh show NAME` prints what the
 *   file sets once read, `BUILD.zsh status NAME` how far its build has
 *   got. Both run as the user who started this tool, not as root (see
 *   syn_become_real_user()): an edition's $HOME is theirs.
 *
 *   An Arch edition only names an archiso profile, which this tool's own
 *   native pipeline builds (syn_build_run()), SYN layer and all. The rest
 *   are built by BUILD.zsh itself: an LFS edition (SYN-LFS/build.zsh) and
 *   an Arch one with LayerStrat=none (mkarchiso alone, on a copy of the
 *   profile, which the native pipeline would add SYN-SOFTWARE to) — see
 *   syn_tui_dashboard.c's launch_edition_build().
 *
 *   SYN-OS     : The Syntax Operating System
 *   Component  : SYN-ISO-BUILDER (Build)
 *   Author     : William Hayward-Holland (Syntax990)
 *   License    : MIT License
 * ------------------------------------------------------------------------ */
#ifndef SYN_EDITION_H
#define SYN_EDITION_H

#include <stdbool.h>
#include <stddef.h>

#define SYN_EDITION_MAX 64

/* How far an edition's build has got, from `BUILD.zsh status` */
typedef struct {
	int steps;             /* lfs: build.zsh's steps (0 for an arch edition) */
	int done;              /* lfs: how many of them its build.state records */
	char next[64];         /* lfs: the first step not done, "" when all are */
	bool unrecorded;       /* lfs: an image, but no build.state saying what's done */
	char newest_iso[1024]; /* its newest ISO, a full path, or "" */
	char newest_when[32];  /* that ISO's date: "2026-10-08 07:02" */
	long long newest_size; /* and its size in bytes */
} syn_edition_state;

typedef struct {
	char name[64];        /* the file's name without .conf: syn-os-lfs */
	char edition[64];     /* Edition= */
	char summary[256];    /* Summary= */
	char base[16];        /* BaseStrat= arch | lfs */
	char packages[16];    /* PackageStrat= pacman | source | xbps */
	char kernel[16];      /* KernelStrat= arch | syn */
	char layer[16];       /* LayerStrat= syn | none (arch: the profile as it is) */
	char output[16];      /* OutputStrat= iso */
	char profile[256];    /* Profile= (arch: archiso profile, relative to the repo or absolute) */
	char work_dir[1024];  /* WorkDir= (lfs) */
	char lfs_profile[16]; /* LfsProfile= (lfs: full | minimal) */
	syn_edition_state state;
} syn_edition;

/* Every SYN-EDITIONS/NAME.conf in `repo_root`, each read through
 * `BUILD.zsh show`, with its state from `BUILD.zsh status`: SYN-OS's own
 * (LayerStrat=syn) first, then the rest, each by name.
 * Returns how many were read into `out` (at most `max`); one that
 * BUILD.zsh can't read (not valid zsh) is left out. */
int syn_edition_list(const char *repo_root, syn_edition *out, int max);

/* Reads (again) how far edition `e`'s build has got into e->state.
 * Returns false if BUILD.zsh couldn't be run. */
bool syn_edition_read_state(const char *repo_root, syn_edition *e);

/* Whether the real user can run doas without being asked for a
 * password. An LFS build calls doas itself, many times, for its image,
 * mounts and chroot, from inside this tool's build panel, which has no
 * way to type a password into; so it only starts when this is true. */
bool syn_edition_doas_ready(void);

#endif
