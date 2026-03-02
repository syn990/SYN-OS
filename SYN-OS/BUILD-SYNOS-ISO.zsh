#!/bin/zsh
# =====================================================================
#               SYN‑OS — Minimal ISO Build Script
# =====================================================================

# ---- Configuration ---------------------------------------------------
BASE_DIR="/home/syntax990/GithubProjects/SYN-OS/SYN-OS"
WORKDIR="$BASE_DIR/WORKDIR"
PROFILE="$BASE_DIR/SYN-ISO-PROFILE"
OUTPUT="$BASE_DIR/ISO_OUTPUT"

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
read "ok?Continue? (y/n): "

[[ "$ok" =~ ^[Yy]$ ]] || { print -P "Aborted."; exit 1; }

# ---- Cleanup ---------------------------------------------------------
print -P "${YELLOW}Cleaning previous build...${NC}"
rm -rf "$WORKDIR" "$OUTPUT"
mkdir -p "$OUTPUT"

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