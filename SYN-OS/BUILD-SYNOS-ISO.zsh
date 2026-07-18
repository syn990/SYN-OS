#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                   S Y N - O S   I S O   B U I L D E R
#
#   Builds a SYN-OS ISO. Two modes:
#     (no flags)       today's mainline SYN-OS (this working tree)
#     --build=<name>   one of the named historical builds in
#                      SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/
#                      build-manifest.json (see --list-builds) — pulled
#                      from the real, unmodified git history of
#                      syn990/SYN-OS and syn990/SYN-RTOS at the exact
#                      commit and profile directory that shipped
#
#   full/minimal package selection is synos.conf's PackageProfile,
#   resolved at install time (see syn-pacstrap.zsh) — not a build-time
#   flag here.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : BUILD-SYNOS-ISO (Build)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

# ---- Configuration ---------------------------------------------------
# Everything --build touches lives under .syncache/:
#   sources/    bare clones of syn990/SYN-OS + syn990/SYN-RTOS (cached,
#               reused across every named-build run)
#   extracted/  one historical profile tree per named build, pulled from
#               sources/ via git archive
#   isos/       every finished ISO, permanent — WORKDIR/ISO_OUTPUT below
#               are scratch space wiped on every run
BASE_DIR="${0:A:h}"
PROFILE_DEFAULT="$BASE_DIR/SYN-ISO-PROFILE"
# tmpfs speeds up pacstrap/squashfs staging; fall back to disk if /tmp isn't tmpfs.
if findmnt -n -o FSTYPE /tmp 2>/dev/null | grep -qx tmpfs; then
  WORKDIR="/tmp/synos-build-workdir"
else
  WORKDIR="$BASE_DIR/WORKDIR"
fi
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

# No flags: interactive menu rather than silently picking a default.
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
  # BUILD_JSON[1] on a still-scalar variable indexes characters, not lines
  # — (@f) has to be assigned into a real array first to split by line.
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

  # git archive keeps the full profile_path as a tarball prefix (e.g.
  # "SYN-OS/SYN-ISO-PROFILE/..."), so --strip-components has to match how
  # many path segments profile_path actually has, not a fixed 1 — that
  # only worked for single-segment paths like "releng".
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

  # Arch retired [community] in mid-2023, merging its packages into
  # [extra] under the same names — every package still resolves via
  # [extra]'s mirrorlist, so the dead repo block is safe to drop.
  if grep -q "^\[community\]$" "$PROFILE/pacman.conf" 2>/dev/null; then
    print -P "${YELLOW}This build's pacman.conf references the retired [community] repo (merged into [extra] by Arch in mid-2023) — removing the dead repo block so the build resolves against today's mirrors:${NC}"
    sed -i '/^\[community\]$/,/^$/{ /^$/!d }; /^\[community\]$/d' "$PROFILE/pacman.conf"
  fi

  # Only requires grub on this build host when the profile itself declares
  # a grub bootmode — mkarchiso refuses to even validate the profile
  # without it, and most named builds are systemd-boot/syslinux only.
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
# ISO_OUTPUT gets wiped every run. Successful builds are moved out to
# .syncache/isos/<name>.iso right after mkarchiso finishes (see below), so
# this only ever archives an ISO left behind by a run that crashed between
# mkarchiso finishing and that move.
setopt local_options null_glob
PrevIsos=("$OUTPUT"/*.iso)
if (( ${#PrevIsos} > 0 )); then
  mkdir -p "$BUILD_ISOS"
  for f in "${PrevIsos[@]}"; do
    print -P "${YELLOW}Found a leftover ISO in OUTPUT from an interrupted run, archiving before wipe:${NC} ${f:t}"
    mv "$f" "$BUILD_ISOS/"
  done
fi

# mkarchiso bind-mounts host filesystems (sysfs, etc.) into
# WORKDIR/*/airootfs while building. A killed or crashed run can leave
# those mounted, which then makes the plain rm -rf below fail with
# "Read-only file system" — unmount anything still under WORKDIR first.
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
    # Named after the build id (or "mainline") and moved out of OUTPUT,
    # which the next run wipes.
    mkdir -p "$BUILD_ISOS"
    ArchiveName="${BUILD_NAME_ID:-mainline}.iso"
    mv "$ISO" "$BUILD_ISOS/$ArchiveName"
    print -P "${GREEN}✔ ISO build complete:${NC} $BUILD_ISOS/$ArchiveName"
else
    print -P "${RED}✖ Build failed.${NC}"
fi