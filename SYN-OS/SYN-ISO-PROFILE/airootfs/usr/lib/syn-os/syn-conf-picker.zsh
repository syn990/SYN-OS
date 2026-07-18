#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                       S Y N - C O N F - P I C K E R
#
#   TTY-native editor for /etc/syn-os/synos.conf: fzf-pick a key, type a new
#   value, write it back. Exists because rofi can't run here — the live ISO
#   autologins straight to a plain tty1 shell, no compositor. Replaces
#   freehand nano editing for the common case (nano's still there for
#   anything this doesn't cover, e.g. adding a new key).
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-CONF-PICKER (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

source /usr/lib/syn-os/syn-ui.zsh

ConfFile="/etc/syn-os/synos.conf"

if [ ! -w "$ConfFile" ]; then
  syn_ui::error "$ConfFile not writable (run with doas)"
  exit 1
fi

# Only real KEY="value" assignments, not comments or blank lines — same
# shape synos.conf uses throughout, one assignment per line.
Lines=("${(@f)$(grep -nE '^[A-Za-z_][A-Za-z0-9_]*="' "$ConfFile")}")
if [ ${#Lines[@]} -eq 0 ]; then
  syn_ui::error "No KEY=\"value\" lines found in $ConfFile"
  exit 1
fi

# fzf shows "KEY = value", picking one yields that same line back.
Picked="$(printf '%s\n' "${Lines[@]}" | sed -E 's/^([0-9]+):([A-Za-z0-9_]+)="([^"]*)"/\2 = \3/' \
  | fzf --prompt="synos.conf key > " --height=~60% --border --header="Enter to edit, Esc to cancel")"

if [ -z "$Picked" ]; then
  syn_ui::info "Cancelled, nothing changed"
  exit 0
fi

Key="${Picked%% = *}"
CurrentValue="${Picked#* = }"

syn_ui::info "Editing ${Key} (current: \"${CurrentValue}\")"
printf "New value: "
read -r NewValue </dev/tty

if [ -z "$NewValue" ] && [ "$NewValue" != "$CurrentValue" ]; then
  syn_ui::info "Empty input, nothing changed"
  exit 0
fi

# Escape / and & for sed's replacement side; the search side only ever
# matches a literal ^Key=" anchor, no user input there.
EscapedValue="${NewValue//\\/\\\\}"
EscapedValue="${EscapedValue//\//\\/}"
EscapedValue="${EscapedValue//&/\\&}"

sed -i -E "s/^${Key}=\"[^\"]*\"/${Key}=\"${EscapedValue}\"/" "$ConfFile"
syn_ui::step_done "${Key} set to \"${NewValue}\""
