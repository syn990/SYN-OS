#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                   S Y N - R E D S H I R T - P R O M P T
#
#   rofi front-end for syn-redshirt.zsh: asks for a file path (redshirt
#   itself detects encrypt vs. decrypt from the file's own header), then
#   runs the real work inside syn_popup::run so the terminal window
#   closes itself when it's done instead of sitting open forever.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-REDSHIRT-PROMPT (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
source /usr/lib/syn-os/syn-popup-lib.zsh
syn_theme_load

file="$(syn_pick::rofi_input "File to encrypt/decrypt (Redshirt):" "$HOME/")"
[[ -n "$file" ]] || exit 0
file="${file/#\~/$HOME}"

syn_popup::run zsh -c '
  base="$1"
  zsh /usr/lib/syn-os/syn-redshirt.zsh "$2"
  rc=$?
  if (( rc == 0 )); then
    notify-send "Redshirt" "Succeeded: $base" 2>/dev/null || true
  else
    notify-send -u critical "Redshirt" "Failed: $base" 2>/dev/null || true
  fi
  exit $rc
' -- "${file:t}" "$file"
