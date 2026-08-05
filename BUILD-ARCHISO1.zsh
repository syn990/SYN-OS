#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                  S Y N - O S   I S O   B U I L D E R   ( v1 )
#
#   zsh reimplementation of SYN-SOFTWARE/syn-iso-builder-src (the native
#   C/ncurses rewrite) — that tool started life as this family of script,
#   moved to C for a real persistent TUI dashboard, and this port brings
#   its actual design decisions back into zsh: a re-enterable session menu
#   instead of one-shot flags, build state anchored outside the repo
#   checkout, a single-instance lock, and repo auto-detection so this
#   script works run from anywhere, not just from inside the checkout.
#   Doesn't attempt the C tool's ncurses screen (zsh has no ncurses
#   binding) — this is a `read`-driven menu loop in the same style
#   BUILD-ARCHISO.zsh already uses, redrawn each time back to one summary
#   + menu screen, not a wizard-then-exit chain.
#
#   Scope is deliberately the same three targets the C tool settled on
#   after an explicit user scope-down ("just design a simple fucking
#   builder that does profile.sh or a commit yo or a local main") —
#   current working tree, a browsed commit, or a local profile directory.
#   No --build=<name>/build-manifest.json support — that stays dropped,
#   same as the C tool. If named historical builds are ever needed again,
#   BUILD-ARCHISO.zsh (unchanged, alongside this file) already has them.
#
#   Whole script runs as root (see Root Check below), same lesson
#   BUILD-ARCHISO.zsh's own header and the C tool's main.c both
#   independently arrived at: splitting privilege between an unprivileged
#   phase and a doas-only mkarchiso call made every step touching scratch
#   space guess whether a prior run's root-owned leftovers made it
#   privileged or not — a real, recurring source of bugs, not a
#   theoretical one.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : BUILD-ARCHISO1 (Build)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

# ---- Colours (zsh prompt escapes; use print -P) ----------------------
RED=$'%F{1}'
GREEN=$'%F{2}'
YELLOW=$'%F{3}'
BLUE=$'%F{4}'
DIM=$'%F{8}'
NC=$'%f'

# ---- Real invoking user's home ----------------------------------------
# Once re-exec'd under doas/sudo below, $HOME is root's own home, not the
# invoking user's — resolve via $DOAS_USER/$SUDO_USER, same fallback
# chain syn_build_paths.c's syn_resolve_real_home() uses. Needed before
# the Root Check itself decides whether to re-exec.
resolve_real_home() {
  local invoking_user="${DOAS_USER:-${SUDO_USER:-}}"
  if [[ -n "$invoking_user" ]]; then
    local pw_home
    pw_home="$(getent passwd "$invoking_user" 2>/dev/null | cut -d: -f6)"
    [[ -n "$pw_home" ]] && { print -r -- "$pw_home"; return; }
  fi
  print -r -- "${HOME:-}"
}
REAL_HOME="$(resolve_real_home)"

# ---- Root Check --------------------------------------------------------
# Re-exec under doas/sudo rather than just erroring, same as the C tool's
# main.c — launching this directly always works without the caller
# needing to remember to prefix a privilege-escalation command themselves.
if [[ $EUID -ne 0 ]]; then
  if command -v doas >/dev/null 2>&1; then
    exec doas "$0" "$@"
  elif command -v sudo >/dev/null 2>&1; then
    exec sudo "$0" "$@"
  else
    print -P "${RED}Run as root. Try: sudo $0  or  doas $0${NC}"
    exit 1
  fi
fi

# ---- Build state: fixed XDG data location, NOT inside the repo --------
# Matches syn_build_paths.h's own reasoning verbatim: build state (multi-
# GB scratch trees, pacstrap output, finished ISOs) used to live under
# <repo>/.syncache/ (BUILD-ARCHISO.zsh still does this), which drags
# gigabytes of build state along with any clone/move/delete of the source
# tree, and dumps huge dirs into the same tree `git status` walks. Same
# ~/.local/share/syn-os/... convention the repo already uses elsewhere.
STATE_ROOT="$REAL_HOME/.local/share/syn-os/iso-builder"
SOURCES_DIR="$STATE_ROOT/sources"
SCRATCH_DIR="$STATE_ROOT/scratch"
EXTRACTED_DIR="$STATE_ROOT/extracted"
OUTPUT_DIR_SCRATCH="$STATE_ROOT/output"
ISOS_DIR="$STATE_ROOT/isos"
LOCK_FILE="$STATE_ROOT/.lock"

mkdir -p "$STATE_ROOT" "$SOURCES_DIR" "$SCRATCH_DIR" "$EXTRACTED_DIR" "$OUTPUT_DIR_SCRATCH" "$ISOS_DIR"

MAIN_REMOTE="https://github.com/syn990/SYN-OS.git"
MAIN_MIRROR="$SOURCES_DIR/syn-os.git"

# ---- Single-instance lock ----------------------------------------------
# flock held for the life of this process (fd 9), released automatically
# on exit or crash — no stale-lockfile cleanup problem. Confirmed
# necessary in the C tool: two instances against the same scratch path
# raced mid-build, one process's cmake build directories
# disappearing/changing under the other's.
exec 9>"$LOCK_FILE"
if ! flock -n 9; then
  print -P "${RED}Another instance is already running (holds the lock at $LOCK_FILE) — only one build at a time.${NC}"
  exit 1
fi

# ---- Resolve the SYN-OS checkout (repo_root) ---------------------------
# Autodetect from cwd upward first (this script's own dir is one obvious
# candidate but not the only one — running this from a checkout elsewhere
# on disk should still work, unlike BUILD-ARCHISO.zsh which is pinned to
# its own BASE_DIR). Falls back to a saved config, then prompts (and
# offers to clone) — same three-tier resolution as syn_repo_locate.c.
looks_like_checkout() {
  [[ -f "$1/SYN-ISO-PROFILE/profiledef.sh" && -d "$1/SYN-SOFTWARE" ]]
}

autodetect_repo_root() {
  local cursor="$PWD"
  local depth=0
  while (( depth < 8 )); do
    if looks_like_checkout "$cursor"; then
      print -r -- "$cursor"
      return 0
    fi
    [[ "$cursor" == "/" ]] && break
    cursor="${cursor:h}"
    (( depth++ ))
  done
  return 1
}

REPO_CONFIG="$REAL_HOME/.config/syn-os/iso-builder.conf"
REPO_SOURCE=""
REPO_ROOT=""

if REPO_ROOT="$(autodetect_repo_root)"; then
  REPO_SOURCE="auto-detected"
elif [[ -r "$REPO_CONFIG" ]]; then
  candidate="$(grep '^repo_path=' "$REPO_CONFIG" | head -1 | cut -d= -f2-)"
  if [[ -n "$candidate" ]] && looks_like_checkout "$candidate"; then
    REPO_ROOT="$candidate"
    REPO_SOURCE="from config"
  fi
fi

if [[ -z "$REPO_ROOT" ]]; then
  print -P "${YELLOW}Could not auto-detect a SYN-OS checkout from $PWD or from a saved config.${NC}"
  suggestion="$REAL_HOME/.local/share/syn-os/SYN-OS"
  read "chosen?SYN-OS checkout location (will clone if empty) [$suggestion]: "
  [[ -z "$chosen" ]] && chosen="$suggestion"
  if ! looks_like_checkout "$chosen"; then
    print -P "${BLUE}Cloning $MAIN_REMOTE into $chosen...${NC}"
    mkdir -p "${chosen:h}"
    git clone "$MAIN_REMOTE" "$chosen" || { print -P "${RED}Clone failed.${NC}"; exit 1; }
    looks_like_checkout "$chosen" || { print -P "${RED}$chosen has no profiledef.sh/SYN-SOFTWARE after cloning.${NC}"; exit 1; }
  fi
  mkdir -p "${REPO_CONFIG:h}"
  print -r -- "repo_path=$chosen" > "$REPO_CONFIG"
  REPO_ROOT="$chosen"
  REPO_SOURCE="just set"
fi

SYN_SOFTWARE_DIR="$REPO_ROOT/SYN-SOFTWARE"

# ---- Mount cleanup ------------------------------------------------------
# mkarchiso bind-mounts host filesystems into scratch/*/airootfs while
# building. A killed/crashed run can leave those mounted, which then
# makes a plain rm -rf fail with "Read-only file system" — unmount
# anything still under $SCRATCH_DIR first, deepest paths first so a
# parent's unmount never runs while a child mount underneath is still
# mounted.
mount_cleanup() {
  local prefix="$1"
  local -a mounts
  mounts=("${(@f)$(mount | awk -v wd="$prefix" '$3 ~ "^"wd {print $3}' | sort -r)}")
  [[ -z "${mounts[1]:-}" ]] && return
  for mp in "${mounts[@]}"; do
    [[ -z "$mp" ]] && continue
    print -P "${YELLOW}Unmounting stray mount left by a previous build:${NC} $mp"
    umount "$mp" 2>/dev/null || umount -l "$mp" 2>/dev/null || true
  done
}

# ---- Target resolution ---------------------------------------------------
# Fills these globals, mirroring syn_build_target's fields: TARGET_KIND
# (current_tree / commit / local_profile), TARGET_PROFILE_DIR,
# TARGET_BUILD_ID, TARGET_LABEL, plus kind-specific detail vars.
TARGET_KIND=""
TARGET_PROFILE_DIR=""
TARGET_BUILD_ID=""
TARGET_LABEL=""
TARGET_COMMIT_SHA=""
TARGET_COMMIT_SUBJECT=""
TARGET_COMMIT_AUTHOR=""
TARGET_COMMIT_DATE=""

set_target_current_tree() {
  TARGET_KIND="current_tree"
  TARGET_PROFILE_DIR="$REPO_ROOT/SYN-ISO-PROFILE"
  TARGET_BUILD_ID="local-$(date +%Y-%m-%d)"
  TARGET_LABEL="Current working tree ($REPO_ROOT), uncommitted changes included"
}

set_target_local_profile() {
  local profpath="$1"
  local resolved="${profpath:A}"
  if [[ ! -f "$resolved/profiledef.sh" ]]; then
    print -P "${RED}$resolved has no profiledef.sh.${NC}"
    return 1
  fi
  TARGET_KIND="local_profile"
  TARGET_PROFILE_DIR="$resolved"
  TARGET_BUILD_ID="local-profile-$(date +%Y-%m-%d)"
  TARGET_LABEL="Local profile directory: $resolved"
}

ensure_main_mirror() {
  mkdir -p "$SOURCES_DIR"
  if [[ ! -d "$MAIN_MIRROR" ]] || ! git --git-dir="$MAIN_MIRROR" rev-parse --git-dir >/dev/null 2>&1; then
    rm -rf "$MAIN_MIRROR"
    print -P "${BLUE}Cloning $MAIN_REMOTE (one-time, cached at $MAIN_MIRROR)...${NC}"
    git clone --mirror "$MAIN_REMOTE" "$MAIN_MIRROR" || return 1
  else
    print -P "${BLUE}Updating commit history mirror...${NC}"
    git --git-dir="$MAIN_MIRROR" fetch --quiet origin '+refs/heads/*:refs/heads/*' 2>/dev/null || true
  fi
  return 0
}

set_target_browse_commit() {
  ensure_main_mirror || { print -P "${RED}Could not clone/update that repository. Check your network.${NC}"; return 1; }

  if ! command -v fzf >/dev/null 2>&1; then
    print -P "${RED}fzf not installed — can't browse commit history interactively.${NC}"
    return 1
  fi

  local picked
  picked="$(git --git-dir="$MAIN_MIRROR" log --pretty=format:'%h  %as  %an  %s' \
    | fzf --prompt="Pick a commit> " --header="Enter to select, Esc to cancel")"
  [[ -n "$picked" ]] || { print -P "Aborted."; return 1; }

  local short_sha="${picked%%  *}"
  local full_sha
  full_sha="$(git --git-dir="$MAIN_MIRROR" rev-parse "$short_sha")"
  local commit_date author subject
  commit_date="$(git --git-dir="$MAIN_MIRROR" show -s --format=%as "$full_sha")"
  author="$(git --git-dir="$MAIN_MIRROR" show -s --format=%an "$full_sha")"
  subject="$(git --git-dir="$MAIN_MIRROR" show -s --format=%s "$full_sha")"

  local dest="$EXTRACTED_DIR/${full_sha:0:12}"
  print -P "${BLUE}Extracting commit tree...${NC}"
  rm -rf "$dest"
  mkdir -p "$dest"
  git --git-dir="$MAIN_MIRROR" archive "$full_sha" | tar -x -C "$dest"

  local found
  found="$(find "$dest" -maxdepth 4 -name profiledef.sh -print -quit 2>/dev/null)"
  if [[ -z "$found" ]]; then
    print -P "${RED}No profiledef.sh found within 4 levels of $short_sha's tree.${NC}"
    return 1
  fi

  TARGET_KIND="commit"
  TARGET_PROFILE_DIR="${found:h}"
  TARGET_BUILD_ID="syn-os-${commit_date}-${short_sha}"
  TARGET_LABEL="$short_sha — $subject ($commit_date, $author)"
  TARGET_COMMIT_SHA="$short_sha"
  TARGET_COMMIT_SUBJECT="$subject"
  TARGET_COMMIT_AUTHOR="$author"
  TARGET_COMMIT_DATE="$commit_date"
}

pick_target() {
  print -P ""
  print -P "${YELLOW}Set build target:${NC}"
  print -P "  ${BLUE}1)${NC} Current working tree"
  print -P "  ${BLUE}2)${NC} Browse commit history"
  print -P "  ${BLUE}3)${NC} Local profile directory"
  print -P "  ${BLUE}0)${NC} Cancel"
  read "choice?Pick a number: "
  case "$choice" in
    1) set_target_current_tree ;;
    2) set_target_browse_commit ;;
    3)
      read "profpath?Path to a directory containing profiledef.sh: "
      [[ -n "$profpath" ]] || { print -P "Aborted."; return 1; }
      set_target_local_profile "$profpath"
      ;;
    *) return 1 ;;
  esac
}

# ---- Build runner ---------------------------------------------------------
# Same seven steps as syn_build_run(): wipe+recreate scratch/output, mount
# cleanup before the wipe, SYN-SOFTWARE build loop, retired [community]
# repo fix, grub presence check, mkarchiso, archive.
run_build() {
  local out_dir="$1" out_name="$2"

  print -P ""
  print -P "${BLUE}Profile:${NC} $TARGET_PROFILE_DIR"
  print -P "${BLUE}Output:${NC}  $out_dir/$out_name.iso"
  print -P "${BLUE}Build:${NC}   $TARGET_LABEL"
  read "ok?Continue? (y/n): "
  [[ "$ok" =~ ^[Yy]$ ]] || { print -P "Aborted."; return 1; }

  # Step 1: mount cleanup, then wipe+recreate scratch/output.
  mount_cleanup "$SCRATCH_DIR"
  rm -rf "$SCRATCH_DIR" "$OUTPUT_DIR_SCRATCH"
  if [[ -d "$SCRATCH_DIR" ]]; then
    print -P "${RED}Could not fully remove $SCRATCH_DIR (still mounted, or a permissions issue) — aborting rather than building on top of a stale tree.${NC}"
    return 1
  fi
  mkdir -p "$SCRATCH_DIR" "$OUTPUT_DIR_SCRATCH"

  if findmnt -n -o FSTYPE /tmp 2>/dev/null | grep -qx tmpfs; then
    print -P "${DIM}tmpfs detected at /tmp — building on disk at $SCRATCH_DIR (not /tmp); bind-mount a tmpfs there yourself first if you want the speed-up.${NC}"
  else
    print -P "${DIM}Building on disk at $SCRATCH_DIR.${NC}"
  fi

  # Step 3: SYN-SOFTWARE build loop, staged into this target's own
  # profile_dir/airootfs — never a sibling of the source dir, unlike an
  # earlier version of this idea, so nothing lands inside the tracked
  # SYN-SOFTWARE/ tree itself.
  local software_log_dir="$SCRATCH_DIR/syn-software-build-logs"
  mkdir -p "$software_log_dir"
  if [[ -d "$SYN_SOFTWARE_DIR" ]]; then
    for ProjDir in "$SYN_SOFTWARE_DIR"/*-src(N/); do
      local ToolName="${ProjDir:t:r:s/-src/}"
      print -P "${BLUE}Building ${ToolName} for the live environment...${NC}"
      local ToolBuild="$SCRATCH_DIR/software-build/${ToolName}"
      local log_path="$software_log_dir/${ToolName}.log"
      if cmake -B "$ToolBuild" -S "$ProjDir" -DCMAKE_BUILD_TYPE=Release >"$log_path" 2>&1 \
          && cmake --build "$ToolBuild" >>"$log_path" 2>&1 \
          && DESTDIR="$TARGET_PROFILE_DIR/airootfs" cmake --install "$ToolBuild" --prefix /usr >>"$log_path" 2>&1; then
        print -P "${GREEN}${ToolName} built and staged into the live environment.${NC}"
      else
        print -P "${RED}${ToolName} build failed — it won't be available on this ISO. Continuing without it.${NC}"
        print -P "  Full log: $log_path"
        print -P "  Last 15 lines:"
        tail -n 15 "$log_path" | while IFS= read -r line; do print -P "    $line"; done
      fi
    done
    print -P "${DIM}Per-tool build logs saved under: $software_log_dir${NC}"
  fi

  # Step 4: retired [community] repo fix — Arch merged it into [extra] in
  # mid-2023 under the same package names.
  local pacman_conf="$TARGET_PROFILE_DIR/pacman.conf"
  if [[ -f "$pacman_conf" ]] && grep -q "^\[community\]$" "$pacman_conf"; then
    print -P "${YELLOW}This build's pacman.conf references the retired [community] repo (merged into [extra] by Arch in mid-2023) — removing the dead repo block so the build resolves against today's mirrors:${NC}"
    sed -i '/^\[community\]$/,/^$/{ /^$/!d }; /^\[community\]$/d' "$pacman_conf"
  fi

  # Step 5: grub presence, only if this profile needs it.
  if grep -q "uefi.grub\|bios.grub" "$TARGET_PROFILE_DIR/profiledef.sh" 2>/dev/null; then
    if ! command -v grub-install >/dev/null 2>&1; then
      print -P "${YELLOW}This build's profiledef.sh declares a grub bootmode, but grub isn't installed on this host.${NC}"
      print -P "${YELLOW}Installing it now (this doesn't touch this host's own bootloader):${NC}"
      pacman -Sy --noconfirm --needed grub || { print -P "${RED}Failed to install grub — can't build this profile without it.${NC}"; return 1; }
    fi
  fi

  # Step 6: the one privileged step.
  print -P ""
  print -P "${BLUE}Building SYN-OS ISO (mkarchiso)...${NC}"
  mkarchiso -v -w "$SCRATCH_DIR" -o "$OUTPUT_DIR_SCRATCH" "$TARGET_PROFILE_DIR"
  local status=$?

  if [[ $status -ne 0 ]]; then
    print -P "${RED}✖ Build failed.${NC}"
    return 1
  fi

  # Step 7: archive — move the .iso to out_dir/out_name.iso, copy the
  # per-tool build logs alongside it (scratch/ gets wiped next run).
  local iso
  iso="$(ls "$OUTPUT_DIR_SCRATCH"/*.iso 2>/dev/null | head -n1)"
  if [[ -z "$iso" ]]; then
    print -P "${RED}mkarchiso succeeded but no .iso was found in $OUTPUT_DIR_SCRATCH — something's wrong.${NC}"
    return 1
  fi
  mkdir -p "$out_dir"
  local dest="$out_dir/$out_name.iso"
  mv "$iso" "$dest"

  if [[ -d "$software_log_dir" ]]; then
    local log_dest="$out_dir/${out_name}-build-logs"
    rm -rf "$log_dest"
    cp -r "$software_log_dir" "$log_dest"
  fi

  print -P "${GREEN}✔ ISO build complete:${NC} $dest"
  return 0
}

# ---- Session loop -------------------------------------------------------
# Re-enterable summary + menu, redrawn each pass — sets a target once,
# tweaks output if wanted, launches, and lands back on the same screen
# instead of exiting after one build (a full re-run of a repo-detecting,
# lock-taking, mirror-checking script per build would be unnecessary
# overhead the C tool's persistent dashboard avoided entirely).
set_target_current_tree  # sensible default, matches today's implicit start

OUT_DIR="$ISOS_DIR"
OUT_NAME="$TARGET_BUILD_ID"
OUT_NAME_CUSTOMIZED=0

while true; do
  [[ $OUT_NAME_CUSTOMIZED -eq 0 ]] && OUT_NAME="$TARGET_BUILD_ID"

  print -P ""
  print -P "${YELLOW}=== SYN-ISO-BUILDER (v1) ===${NC}"
  print -P "${BLUE}Repo (${REPO_SOURCE}):${NC} $REPO_ROOT"
  print -P "${BLUE}Target:${NC} $TARGET_LABEL"
  if [[ "$TARGET_KIND" == "commit" ]]; then
    print -P "  ${DIM}SHA: $TARGET_COMMIT_SHA   By: $TARGET_COMMIT_AUTHOR   Date: $TARGET_COMMIT_DATE${NC}"
  fi
  print -P "${BLUE}Scratch:${NC} $SCRATCH_DIR"
  print -P "${BLUE}Output:${NC}  $OUT_DIR/$OUT_NAME.iso"
  print -P ""
  print -P "  ${BLUE}1)${NC} Set target"
  print -P "  ${BLUE}2)${NC} Set output directory/filename"
  print -P "  ${BLUE}3)${NC} Launch build"
  print -P "  ${BLUE}0)${NC} Quit"
  read "action?Pick a number: "

  case "$action" in
    1) pick_target ;;
    2)
      read "newdir?Output directory [$OUT_DIR]: "
      [[ -n "$newdir" ]] && OUT_DIR="${newdir:A}"
      read "newname?ISO filename without .iso [$OUT_NAME]: "
      if [[ -n "$newname" ]]; then
        OUT_NAME="$newname"
        OUT_NAME_CUSTOMIZED=1
      fi
      ;;
    3) run_build "$OUT_DIR" "$OUT_NAME" ;;
    0|q|Q) print -P "Bye."; exit 0 ;;
    *) print -P "${RED}Not a valid choice.${NC}" ;;
  esac
done
