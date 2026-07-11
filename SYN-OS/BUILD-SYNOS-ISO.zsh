#!/bin/zsh
# =====================================================================
#               SYN‑OS — ISO Build Script
# =====================================================================
# Two ways to use this:
#   --mode=minimal|full   build TODAY's mainline SYN-OS (default)
#   --build=<name>         build one of the named builds in
#                          docs/build-manifest.json instead
#                          (see --list-builds)
#
# --build pulls from the ACTUAL git history of syn990/SYN-OS and
# syn990/SYN-RTOS — the exact commit and exact archiso profile directory
# that shipped at the time, unmodified. Nothing is re-authored or
# backfilled; if an old profile genuinely can't build with a current
# mkarchiso (deprecated archiso conventions, packages dropped from Arch's
# repos since), that's a real, informative failure, not something this
# script tries to paper over.
#
# Every named build in docs/build-manifest.json already has a real,
# self-contained, mkarchiso-buildable profile (profiledef.sh +
# packages.x86_64 + airootfs) — even the earliest one. The truly manual
# predecessor to that (boot vanilla archlinux.org ISO, pacman -S git,
# clone the repo by hand, run the raw installer script live) came before
# any of these profiles existed, and was abandoned specifically because
# doing all that inside the live ISO's tmpfs sometimes ran the session
# out of storage — which is why a real baked-in packages.x86_64 profile
# exists at all. Nothing to reproduce here; every entry below is already
# the fix for that.

# ---- Configuration ---------------------------------------------------
# Everything --build touches lives under one .syncache/ directory, named for
# what it actually is:
#   .syncache/sources/    full bare clones of syn990/SYN-OS + syn990/SYN-RTOS
#                      (downloaded once, real project history, reused
#                      across every named-build run)
#   .syncache/extracted/  one real historical profile tree per named build,
#                      pulled out of sources/ via git archive
#   .syncache/isos/       every finished ISO, permanent — mkarchiso's own
#                      WORKDIR/ISO_OUTPUT below are scratch space wiped on
#                      every run, nothing is ever kept there
BASE_DIR="/home/syntax990/GithubProjects/SYN-OS/SYN-OS"
WORKDIR="$BASE_DIR/WORKDIR"
OUTPUT="$BASE_DIR/ISO_OUTPUT"
DOCS_DIR="$BASE_DIR/../docs"
MANIFEST="$DOCS_DIR/build-manifest.json"
BUILD_ROOT="$BASE_DIR/.syncache"
SOURCES_DIR="$BUILD_ROOT/sources"
EXTRACTED_DIR="$BUILD_ROOT/extracted"
BUILD_ISOS="$BUILD_ROOT/isos"
# Host-side scratch cachedir for named --build profiles. Deliberately
# OUTSIDE $PROFILE — confirmed live that mainline's usr/lib/syn-os/pkgcache
# convention grafts a modern path onto every extracted historical build,
# real or not (found in V4's 2023 tree, which has no usr/lib at all in the
# real commit — checked with git ls-tree against the raw commit object).
# Checked every named build's real, committed pacman.conf: none of them
# reference a [synos-local] repo at all — that's purely a current-mainline
# concept, so named builds never needed this path in the first place.
NAMED_BUILD_PKGCACHE="$BUILD_ROOT/pkgcache"
MAIN_MIRROR="$SOURCES_DIR/syn-os.git"
RTOS_MIRROR="$SOURCES_DIR/syn-rtos.git"
MAIN_REMOTE="https://github.com/syn990/SYN-OS.git"
RTOS_REMOTE="https://github.com/syn990/SYN-RTOS.git"

# ---- Colours (zsh prompt escapes; use print -P) ----------------------
RED=$'%F{1}'
GREEN=$'%F{2}'
YELLOW=$'%F{3}'
BLUE=$'%F{4}'
NC=$'%f'

# ---- Parse args --------------------------------------------------------
BUILD_MODE=""
BUILD_NAME_ID=""
LIST_BUILDS=0
FLAGS_GIVEN=0
for arg in "$@"; do
  FLAGS_GIVEN=1
  case "$arg" in
    --mode=*)      BUILD_MODE="${arg#--mode=}" ;;
    --build=*)       BUILD_NAME_ID="${arg#--build=}" ;;
    --list-builds)   LIST_BUILDS=1 ;;
  esac
done

# ---- --list-builds: just print the manifest and exit, no root needed ---
if [[ $LIST_BUILDS -eq 1 ]]; then
  if [[ ! -r "$MANIFEST" ]]; then
    print -P "${RED}Manifest not found at $MANIFEST${NC}"
    exit 1
  fi
  python3 -c "
import json
with open('$MANIFEST') as f:
    data = json.load(f)
def weight(n):
    if n < 40: return 'thin'
    if n < 100: return 'medium'
    return 'fat'
for e in data:
    stack = e['desktop_stack'].split(';')[0].split('(')[0].strip()
    w = weight(e['package_count'])
    print(f\"  {e['id']:20} {e['commit_date']}  [{w:6} {e['package_count']:3}pkg]  {stack}\")
"
  exit 0
fi

# ---- No flags at all: interactive menu instead of silently picking a
# default. This used to fall through to --mode=minimal with zero
# indication that was happening — a numbered menu is a lot harder to
# run by accident.
if [[ $FLAGS_GIVEN -eq 0 ]]; then
  print -P "${YELLOW}SYN-OS ISO builder — what do you want to build?${NC}"
  print -P "  ${BLUE}1)${NC} Current SYN-OS — minimal (faster, base + desktop stack only)"
  print -P "  ${BLUE}2)${NC} Current SYN-OS — full (everything, including apps/dev-tools)"
  if [[ -r "$MANIFEST" ]]; then
    print -P "  ${BLUE}--${NC} Named builds (built from real git history) ${BLUE}--${NC}"
    BUILD_MENU_LINES="$(python3 -c "
import json
with open('$MANIFEST') as f:
    data = json.load(f)
def weight(n):
    if n < 40: return 'thin'
    if n < 100: return 'medium'
    return 'fat'
for i, e in enumerate(data, start=3):
    w = weight(e['package_count'])
    print(f\"{i}|{e['id']}|{e['commit_date']}  {e['name'].split('(')[0].strip()} [{w}, {e['package_count']} pkgs]\")
")"
    typeset -a BuildMenuIds
    for line in "${(f)BUILD_MENU_LINES}"; do
      num="${line%%|*}"
      rest="${line#*|}"
      id="${rest%%|*}"
      label="${rest#*|}"
      print -P "  ${BLUE}${num})${NC} $label"
      BuildMenuIds[$num]="$id"
    done
  fi
  print -P ""
  read "choice?Pick a number (or Ctrl+C to cancel): "
  case "$choice" in
    1) BUILD_MODE="minimal" ;;
    2) BUILD_MODE="full" ;;
    *)
      if [[ -n "${BuildMenuIds[$choice]:-}" ]]; then
        BUILD_NAME_ID="${BuildMenuIds[$choice]}"
      else
        print -P "${RED}Not a valid choice.${NC}"
        exit 1
      fi
      ;;
  esac
fi

[[ -z "$BUILD_MODE" && -z "$BUILD_NAME_ID" ]] && BUILD_MODE="minimal"

# --build and --mode are mutually exclusive selections, not layered — a
# named build always uses its own real packages.x86_64, so BUILD_MODE
# only needs validating when no named build was picked (see the
# --mode=full note further down for the one place BUILD_MODE still gets
# read either way).
if [[ -z "$BUILD_NAME_ID" ]]; then
  case "$BUILD_MODE" in
    minimal|full) : ;;
    *) print -P "%F{1}Unknown --mode '$BUILD_MODE'. Use minimal or full.%f"; exit 1 ;;
  esac
fi

# ---- Root Check ------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
    print -P "${RED}Run as root. Try: sudo $0  or  doas $0${NC}"
    exit 1
fi

# ---- Resolve PROFILE: either today's mainline, or a real named build
if [[ -n "$BUILD_NAME_ID" ]]; then
  if [[ ! -r "$MANIFEST" ]]; then
    print -P "${RED}Manifest not found at $MANIFEST — can't resolve --build=$BUILD_NAME_ID${NC}"
    exit 1
  fi

  BUILD_JSON="$(python3 -c "
import json, sys
with open('$MANIFEST') as f:
    data = json.load(f)
match = next((e for e in data if e['id'] == '$BUILD_NAME_ID'), None)
if match is None:
    sys.exit(1)
print(match['repo'])
print(match['commit'])
print(match['profile_path'])
print(match['name'])
")"
  if [[ $? -ne 0 ]]; then
    print -P "${RED}Unknown build '$BUILD_NAME_ID'. Run --list-builds to see valid names.${NC}"
    exit 1
  fi
  # (f) alone splits BUILD_JSON into lines for iteration, but subscripting a
  # still-scalar variable like BUILD_JSON[1] indexes CHARACTERS, not lines —
  # confirmed live: this returned "m"/"a"/"i"/"n" instead of the 4 real
  # fields. (@f) assigned into a real array first is what actually splits
  # it into elements you can index by line number.
  typeset -a BuildFields
  BuildFields=("${(@f)BUILD_JSON}")
  BUILD_REPO="${BuildFields[1]}"
  BUILD_COMMIT="${BuildFields[2]}"
  BUILD_PROFILE_PATH="${BuildFields[3]}"
  BUILD_NAME="${BuildFields[4]}"

  # Clone (once) each source repo as a bare mirror, cached for reuse.
  mkdir -p "$SOURCES_DIR"
  if [[ "$BUILD_REPO" == "main" ]]; then
    MIRROR="$MAIN_MIRROR"; REMOTE="$MAIN_REMOTE"
  else
    MIRROR="$RTOS_MIRROR"; REMOTE="$RTOS_REMOTE"
  fi
  if [[ ! -d "$MIRROR" ]]; then
    print -P "${BLUE}First use of --build: cloning $REMOTE (one-time, cached at $MIRROR)...${NC}"
    git clone --mirror "$REMOTE" "$MIRROR" || { print -P "${RED}Clone failed.${NC}"; exit 1; }
  else
    git --git-dir="$MIRROR" fetch --quiet origin '+refs/heads/*:refs/heads/*' 2>/dev/null || true
  fi

  if ! git --git-dir="$MIRROR" cat-file -e "$BUILD_COMMIT" 2>/dev/null; then
    print -P "${RED}Commit $BUILD_COMMIT not found in $MIRROR (mirror may be stale or manifest wrong).${NC}"
    exit 1
  fi

  # Extract that one commit's profile_path into a scratch dir — nothing
  # else in the checkout is touched, and nothing in this repo's own
  # working tree changes. git archive keeps the full profile_path as a
  # prefix inside the tarball (e.g. "SYN-OS/SYN-ISO-PROFILE/..."), so the
  # strip count has to match how many path segments profile_path actually
  # has — a fixed --strip-components=1 only worked for single-segment
  # paths like "releng" and silently left everything one directory too
  # deep for anything nested, e.g. "SYN-OS/SYN-ISO-PROFILE".
  PROFILE="$EXTRACTED_DIR/$BUILD_NAME_ID"
  rm -rf "$PROFILE"
  mkdir -p "$PROFILE"
  BUILD_PATH_SEGMENTS=("${(@s:/:)BUILD_PROFILE_PATH}")
  STRIP_COUNT=${#BUILD_PATH_SEGMENTS}
  git --git-dir="$MIRROR" archive "$BUILD_COMMIT" -- "$BUILD_PROFILE_PATH" | tar -x -C "$PROFILE" --strip-components="$STRIP_COUNT"
  if [[ ! -f "$PROFILE/profiledef.sh" ]]; then
    print -P "${RED}Extracted tree has no profiledef.sh at its root — profile_path or commit in the manifest may be wrong.${NC}"
    print -P "  Looked for: $BUILD_PROFILE_PATH at $BUILD_COMMIT in $MIRROR"
    exit 1
  fi
  SYNOS_LIB="$PROFILE/airootfs/usr/lib/syn-os"
  PKGCACHE="$NAMED_BUILD_PKGCACHE"
  print -P "${YELLOW}Building historical edition:${NC} $BUILD_NAME"
  print -P "${BLUE}Source:${NC} $BUILD_REPO @ ${BUILD_COMMIT:0:10}"

  # Arch retired the [community] repo in mid-2023, merging its packages into
  # [extra] under the same names — a mirror/repo-index change on Arch's side,
  # not anything about what this build's own install logic or package
  # selection actually was. Goal here is restoring each build's real UX and
  # install strategy, not preserving incidental pacman-repo-layout breakage
  # that has nothing to do with that. Every package still resolves via
  # [extra]'s mirrorlist, so it's safe to just drop the dead repo block.
  if grep -q "^\[community\]$" "$PROFILE/pacman.conf" 2>/dev/null; then
    print -P "${YELLOW}This build's pacman.conf references the retired [community] repo (merged into [extra] by Arch in mid-2023) — removing the dead repo block so the build resolves against today's mirrors:${NC}"
    sed -i '/^\[community\]$/,/^$/{ /^$/!d }; /^\[community\]$/d' "$PROFILE/pacman.conf"
  fi

  # Only require grub on THIS host when the profile being built actually
  # declares a grub bootmode — most named builds are systemd-boot/syslinux
  # only and never touch it. Confirmed live: synaptics's real profiledef.sh
  # declares bootmodes=(bios.syslinux uefi.grub), mkarchiso refused to even
  # validate the profile without grub installed on the build host, and that
  # requirement is real project history (this is what synaptics's own
  # profile actually needed at the time), not something to route around by
  # installing grub unconditionally for every build regardless of whether
  # it's used.
  if grep -q "uefi.grub\|bios.grub" "$PROFILE/profiledef.sh" 2>/dev/null; then
    if ! command -v grub-install >/dev/null 2>&1; then
      print -P "${YELLOW}This build's profiledef.sh declares a grub bootmode, but grub isn't installed on this host.${NC}"
      print -P "${YELLOW}Installing it now (mkarchiso needs grub-install present to validate/build a grub-capable profile — this doesn't touch this host's own bootloader):${NC}"
      pacman -Sy --noconfirm --needed grub || { print -P "${RED}Failed to install grub — can't build this profile without it.${NC}"; exit 1; }
    fi
  fi
else
  PROFILE="$BASE_DIR/SYN-ISO-PROFILE"
  SYNOS_LIB="$PROFILE/airootfs/usr/lib/syn-os"
  # Mainline's own committed pacman.conf hardcodes
  # Server = file:///usr/lib/syn-os/pkgcache for [synos-local], resolved
  # inside the live ISO's own root — this really must live inside $PROFILE
  # for mainline builds, unlike named builds above.
  PKGCACHE="$SYNOS_LIB/pkgcache"
fi

# ---- Confirmation ----------------------------------------------------
print -P "${YELLOW}This will build a fresh SYN‑OS ISO.${NC}"
print -P "${BLUE}Profile:${NC} $PROFILE"
print -P "${BLUE}Output:${NC}  $OUTPUT"
if [[ -n "$BUILD_NAME_ID" ]]; then
  print -P "${BLUE}Build:${NC}   $BUILD_NAME"
else
  print -P "${BLUE}Mode:${NC}    $BUILD_MODE"
fi
read "ok?Continue? (y/n): "

[[ "$ok" =~ ^[Yy]$ ]] || { print -P "Aborted."; exit 1; }

# ---- Cleanup ---------------------------------------------------------
# ISO_OUTPUT gets wiped on every run — real ISOs were getting silently
# destroyed by the next build (confirmed live: building one named build deleted
# the previous one's ISO with no warning). Every successful build now gets moved
# out to .syncache/isos/<name>.iso right after mkarchiso finishes (see below),
# so by the time this runs again there's nothing worth losing left in
# OUTPUT — but archive defensively anyway in case a previous run crashed
# after mkarchiso but before that move.
setopt local_options null_glob
PrevIsos=("$OUTPUT"/*.iso)
if (( ${#PrevIsos} > 0 )); then
  mkdir -p "$BUILD_ISOS"
  for f in "${PrevIsos[@]}"; do
    print -P "${YELLOW}Found a leftover ISO in OUTPUT from an interrupted run, archiving before wipe:${NC} ${f:t}"
    mv "$f" "$BUILD_ISOS/"
  done
fi

# mkarchiso bind-mounts real host filesystems (sysfs, etc.) into
# WORKDIR/*/airootfs while it builds. If a previous run was interrupted
# (killed, crashed, chained builds where one failure took down the rest)
# those mounts can survive it — confirmed live: a failed v4 build left
# WORKDIR/x86_64/airootfs/sys mounted read-only, and the plain rm -rf
# below then failed on every file under it with "Read-only file system",
# which cascaded into every subsequent build in a chained run failing
# the same way even though their own profiles were fine. Unmount
# anything still mounted under WORKDIR first, so one bad build can't
# take the rest of a run down with it.
if [[ -d "$WORKDIR" ]]; then
  mount | awk -v wd="$WORKDIR" '$3 ~ "^"wd {print $3}' | sort -r | while read -r mp; do
    print -P "${YELLOW}Unmounting stray mount left by a previous build:${NC} $mp"
    umount "$mp" 2>/dev/null || umount -l "$mp" 2>/dev/null || true
  done
fi

print -P "${YELLOW}Cleaning previous build...${NC}"
rm -rf "$WORKDIR" "$OUTPUT" "$PKGCACHE"
if [[ -d "$WORKDIR" ]]; then
  print -P "${RED}Couldn't fully remove $WORKDIR (still mounted, or a permissions issue) — aborting rather than building on top of a stale tree.${NC}"
  exit 1
fi
mkdir -p "$OUTPUT" "$PKGCACHE"

# Named-build profiles may be old enough to predate syn-packages.zsh entirely
# (early SYN-RTOS builds used a flat pacstrap line baked directly into the
# installer script, not this file) — full mode's pre-fetch only applies
# when both the file and the array it defines actually exist.
if [[ -n "$BUILD_NAME_ID" ]]; then
  # --mode=full's "pre-fetch every package for offline install" doesn't
  # map cleanly onto an old profile — its packages.x86_64 already IS the
  # full list mkarchiso pulls in directly, there's no separate SYNSTALL
  # concept to pre-fetch for most historical named builds. Historical builds just
  # build with whatever packages.x86_64 really said at the time.
  if [[ "$BUILD_MODE" == "full" ]]; then
    print -P "${YELLOW}Note: --mode=full has no effect on --build — named-build profiles build from their own real packages.x86_64 directly.${NC}"
  fi
elif [[ -r "$SYNOS_LIB/syn-packages.zsh" ]]; then
  source "$SYNOS_LIB/syn-packages.zsh"
fi

# ---- Populate local package cache (full mode only, mainline builds only)
if [[ -z "$BUILD_NAME_ID" && "$BUILD_MODE" == "full" ]]; then
  if (( ${#SYNSTALL[@]} == 0 )); then
    print -P "${RED}--mode=full requires SYNSTALL from syn-packages.zsh, but it's empty or missing.${NC}"
    exit 1
  fi
  print -P "${BLUE}Pre-fetching SYNSTALL packages for offline install...${NC}"
  pacman -Syw --noconfirm --cachedir "$PKGCACHE" "${SYNSTALL[@]}"
fi

# [synos-local] must always resolve to a valid db, even with zero packages,
# or pacman -Sy fails outright in minimal mode. nullglob so an empty cache
# doesn't pass a literal unmatched pattern to repo-add.
# Mainline builds only — checked every named build's real, committed
# pacman.conf directly (git show against the archive mirror) and none of
# them declare a [synos-local] repo at all, so there's nothing for a named
# build to populate here.
if [[ -z "$BUILD_NAME_ID" ]]; then
  setopt local_options null_glob
  PkgFiles=("$PKGCACHE"/*.pkg.tar.zst)
  if (( ${#PkgFiles} > 0 )); then
    repo-add "$PKGCACHE/synos-local.db.tar.gz" "${PkgFiles[@]}"
  else
    tar -czf "$PKGCACHE/synos-local.db.tar.gz" --files-from /dev/null
    ln -sf synos-local.db.tar.gz "$PKGCACHE/synos-local.db"
  fi
fi

# ---- Build -----------------------------------------------------------
print -P "${BLUE}Building SYN‑OS ISO...${NC}"
mkarchiso -v -w "$WORKDIR" -o "$OUTPUT" "$PROFILE"
STATUS=$?

if [[ $STATUS -eq 0 ]]; then
    ISO=$(ls "$OUTPUT"/*.iso 2>/dev/null | head -n1)
    # Move straight out to BUILD_ISOS/<name>.iso, named after the build id
    # (or mainline-<mode> for a non-named-build run) rather than left under
    # mkarchiso's own generated filename in OUTPUT, where the next build
    # would wipe it. This directory is otherwise untouched by the script.
    mkdir -p "$BUILD_ISOS"
    ArchiveName="${BUILD_NAME_ID:-mainline-$BUILD_MODE}.iso"
    mv "$ISO" "$BUILD_ISOS/$ArchiveName"
    print -P "${GREEN}✔ ISO build complete:${NC} $BUILD_ISOS/$ArchiveName"
else
    print -P "${RED}✖ Build failed.${NC}"
fi