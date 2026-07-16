#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                           S C R E E N S H O T
#
#   Takes a screenshot, prompting for an output directory via wmenu first.
#   $1 selects the mode — "full" is the whole output, "region" is an
#   interactive slurp selection. Reports completion via notify-send, same
#   as screen-recorder.zsh's Start/Stop toasts.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SCREENSHOT (Capture)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
syn_theme_load

DEFAULT_DIR="$HOME/Pictures/Screenshots"

CHOSEN_DIR=$(printf '%s\n' \
  "$HOME/Pictures/Screenshots" \
  "$HOME" \
  "$HOME/Videos" \
  "$HOME/Desktop" \
  "$HOME/Downloads" \
  | syn_pick::rofi "Save screenshot to:")

OUT_DIR="${CHOSEN_DIR:-$DEFAULT_DIR}"
# Expand a leading ~ if the user typed one instead of picking a preset.
OUT_DIR="${OUT_DIR/#\~/$HOME}"
mkdir -p "$OUT_DIR"

mode="${1:-full}"
geometry_args=()
if [[ "$mode" == "region" ]]; then
  region="$(slurp)"
  [[ -z "$region" ]] && exit 0
  geometry_args=(-g "$region")
fi

out_file="$OUT_DIR/$(date +%F_%H-%M-%S).png"
grim "${geometry_args[@]}" "$out_file"

notify-send "Screenshot" "Saved -> $out_file" 2>/dev/null || true
