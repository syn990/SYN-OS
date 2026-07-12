#!/bin/zsh
# =====================================================================
#               SYN‑OS — ISO Build Script
# =====================================================================
# Two ways to use this:
#   (no flags)             build TODAY's mainline SYN-OS
#   --build=<name>         build one of the named builds in
#                          SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/build-manifest.json instead
#                          (see --list-builds)
#
# There used to be a --mode=minimal|full build-time flag here, pre-fetching
# SYNSTALL packages into a local pacman repo baked into the ISO for a
# nominally-offline install. Removed: nothing in the installer ever
# actually checked whether that cache was populated and skipped network
# access accordingly, so it only ever made the ISO several GB bigger for
# a capability that didn't exist. The real, working full/minimal switch
# is synos.conf's PackageProfile, resolved at install time inside the
# booted ISO (see syn-pacstrap.zsh) — one declarative config file, same
# pattern as every other install-time strategy choice, not a second flag
# duplicating it at build time.
#
# --build pulls from the ACTUAL git history of syn990/SYN-OS and
# syn990/SYN-RTOS — the exact commit and exact archiso profile directory
# that shipped at the time, unmodified. Nothing is re-authored or
# backfilled; if an old profile genuinely can't build with a current
# mkarchiso (deprecated archiso conventions, packages dropped from Arch's
# repos since), that's a real, informative failure, not something this
# script tries to paper over.
#
# Every named build in SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/build-manifest.json already has a real,
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
PROFILE_DEFAULT="$BASE_DIR/SYN-ISO-PROFILE"
WORKDIR="$BASE_DIR/WORKDIR"
OUTPUT="$BASE_DIR/ISO_OUTPUT"
DOCS_DIR="$PROFILE_DEFAULT/airootfs/usr/share/syn-os/docs"
MANIFEST="$DOCS_DIR/build-manifest.json"
BUILD_ROOT="$BASE_DIR/.syncache"
SOURCES_DIR="$BUILD_ROOT/sources"
EXTRACTED_DIR="$BUILD_ROOT/extracted"
BUILD_ISOS="$BUILD_ROOT/isos"
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
BUILD_NAME_ID=""
LIST_BUILDS=0
FLAGS_GIVEN=0
for arg in "$@"; do
  FLAGS_GIVEN=1
  case "$arg" in
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
# default. A numbered menu is a lot harder to run by accident than a
# bare invocation silently doing something.
if [[ $FLAGS_GIVEN -eq 0 ]]; then
  print -P "${YELLOW}SYN-OS ISO builder — what do you want to build?${NC}"
  print -P "  ${BLUE}1)${NC} Current SYN-OS — this local working tree ($PROFILE_DEFAULT), uncommitted changes included"
  if [[ -r "$MANIFEST" ]]; then
    print -P "  ${BLUE}--${NC} Named builds — fetched fresh from real git history, NOT this working tree ${BLUE}--${NC}"
    BUILD_MENU_LINES="$(python3 -c "
import json
with open('$MANIFEST') as f:
    data = json.load(f)
def weight(n):
    if n < 40: return 'thin'
    if n < 100: return 'medium'
    return 'fat'
for i, e in enumerate(data, start=2):
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
    1) : ;;
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

# ---- Root Check ------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
    print -P "${RED}Run as root. Try: sudo or doas$0${NC}"
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
  PROFILE="$PROFILE_DEFAULT"
  SYNOS_LIB="$PROFILE/airootfs/usr/lib/syn-os"
fi

# ---- Confirmation ----------------------------------------------------
print -P "${YELLOW}This will build a fresh SYN‑OS ISO.${NC}"
print -P "${BLUE}Profile:${NC} $PROFILE"
print -P "${BLUE}Output:${NC}  $OUTPUT"
if [[ -n "$BUILD_NAME_ID" ]]; then
  print -P "${BLUE}Build:${NC}   $BUILD_NAME (fetched from git, not this working tree)"
else
  print -P "${BLUE}Build:${NC}   Current SYN-OS — this local working tree, uncommitted changes included"
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
rm -rf "$WORKDIR" "$OUTPUT"
if [[ -d "$WORKDIR" ]]; then
  print -P "${RED}Couldn't fully remove $WORKDIR (still mounted, or a permissions issue) — aborting rather than building on top of a stale tree.${NC}"
  exit 1
fi
mkdir -p "$OUTPUT"

# ---- Build -----------------------------------------------------------
print -P "${BLUE}Building SYN‑OS ISO...${NC}"
mkarchiso -v -w "$WORKDIR" -o "$OUTPUT" "$PROFILE"
STATUS=$?

if [[ $STATUS -eq 0 ]]; then
    ISO=$(ls "$OUTPUT"/*.iso 2>/dev/null | head -n1)
    # Move straight out to BUILD_ISOS/<name>.iso, named after the build id
    # (or "mainline" for a non-named-build run) rather than left under
    # mkarchiso's own generated filename in OUTPUT, where the next build
    # would wipe it. This directory is otherwise untouched by the script.
    mkdir -p "$BUILD_ISOS"
    ArchiveName="${BUILD_NAME_ID:-mainline}.iso"
    mv "$ISO" "$BUILD_ISOS/$ArchiveName"
    print -P "${GREEN}✔ ISO build complete:${NC} $BUILD_ISOS/$ArchiveName"
else
    print -P "${RED}✖ Build failed.${NC}"
fi