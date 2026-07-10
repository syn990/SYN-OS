#!/bin/zsh
# =====================================================================
#               SYN‑OS — Minimal ISO Build Script
# =====================================================================

# ---- Configuration ---------------------------------------------------
BASE_DIR="/home/syntax990/GithubProjects/SYN-OS/SYN-OS"
WORKDIR="$BASE_DIR/WORKDIR"
PROFILE="$BASE_DIR/SYN-ISO-PROFILE"
OUTPUT="$BASE_DIR/ISO_OUTPUT"
SYNOS_LIB="$PROFILE/airootfs/usr/lib/syn-os"
PKGCACHE="$SYNOS_LIB/pkgcache"

# ---- Mode flag (--mode minimal|full) ----------------------------------
BUILD_MODE="minimal"
for arg in "$@"; do
  case "$arg" in
    --mode=*) BUILD_MODE="${arg#--mode=}" ;;
  esac
done
case "$BUILD_MODE" in
  minimal|full) : ;;
  *) print -P "%F{1}Unknown --mode '$BUILD_MODE'. Use minimal or full.%f"; exit 1 ;;
esac

# ---- Colours (zsh prompt escapes; use print -P) ----------------------
RED=$'%F{1}'
GREEN=$'%F{2}'
YELLOW=$'%F{3}'
BLUE=$'%F{4}'
NC=$'%f'

# ---- Root Check ------------------------------------------------------
if [[ $EUID -ne 0 ]]; then
    print -P "${RED}Run as root. Try: sudo or doas$0${NC}"
    exit 1
fi

# ---- Confirmation ----------------------------------------------------
print -P "${YELLOW}This will build a fresh SYN‑OS ISO.${NC}"
print -P "${BLUE}Profile:${NC} $PROFILE"
print -P "${BLUE}Output:${NC}  $OUTPUT"
print -P "${BLUE}Mode:${NC}    $BUILD_MODE"
read "ok?Continue? (y/n): "

[[ "$ok" =~ ^[Yy]$ ]] || { print -P "Aborted."; exit 1; }

# ---- Cleanup ---------------------------------------------------------
print -P "${YELLOW}Cleaning previous build...${NC}"
rm -rf "$WORKDIR" "$OUTPUT" "$PKGCACHE"
mkdir -p "$OUTPUT" "$PKGCACHE"

source "$SYNOS_LIB/syn-packages.zsh"

# ---- Populate local package cache (full mode only) ---------------------
if [[ "$BUILD_MODE" == "full" ]]; then
  print -P "${BLUE}Pre-fetching SYNSTALL packages for offline install...${NC}"
  pacman -Syw --noconfirm --cachedir "$PKGCACHE" "${SYNSTALL[@]}"
fi

# [synos-local] must always resolve to a valid db, even with zero packages,
# or pacman -Sy fails outright in minimal mode. nullglob so an empty cache
# doesn't pass a literal unmatched pattern to repo-add.
setopt local_options null_glob
PkgFiles=("$PKGCACHE"/*.pkg.tar.zst)
if (( ${#PkgFiles} > 0 )); then
  repo-add "$PKGCACHE/synos-local.db.tar.gz" "${PkgFiles[@]}"
else
  tar -czf "$PKGCACHE/synos-local.db.tar.gz" --files-from /dev/null
  ln -sf synos-local.db.tar.gz "$PKGCACHE/synos-local.db"
fi

# ---- Build -----------------------------------------------------------
print -P "${BLUE}Building SYN‑OS ISO...${NC}"
mkarchiso -v -w "$WORKDIR" -o "$OUTPUT" "$PROFILE"
STATUS=$?

if [[ $STATUS -eq 0 ]]; then
    ISO=$(ls "$OUTPUT"/*.iso 2>/dev/null | head -n1)
    print -P "${GREEN}✔ ISO build complete:${NC} $ISO"
else
    print -P "${RED}✖ Build failed.${NC}"
fi