#!/usr/bin/env zsh
# Takes a screenshot, prompting for an output directory via wmenu (preset
# shortcuts + free-text entry, same pattern as wifi-menu.zsh). Drops a
# short-lived marker file so waybar's custom/capture-status module can
# flash "Saved" for a few seconds without needing to be pushed to (it
# just polls whether the marker is fresh). $1 selects the mode:
#   full    whole output, no prompt
#   region  interactive slurp selection
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

MARKER="$XDG_RUNTIME_DIR/syn-capture-status"

CURRENT_THEME_FILE="$HOME/.config/syn-os/current-theme"
THEMES_DIR="$HOME/.config/syn-os/themes"
theme_name="SYN-OS-RED"
[ -f "$CURRENT_THEME_FILE" ] && theme_name="$(<"$CURRENT_THEME_FILE")"
theme_file="$THEMES_DIR/$theme_name.theme"
[ -f "$theme_file" ] && source "$theme_file"
SYN_BG="${SYN_BG:-#000000}"
SYN_TEXT="${SYN_TEXT:-#ffffff}"
SYN_ACCENT="${SYN_ACCENT:-#800000}"
SYN_ACCENT_DIM="${SYN_ACCENT_DIM:-#260101}"

DEFAULT_DIR="$HOME/Pictures/Screenshots"

# -l 5: vertical list of all 5 presets visible at once, not a single-line
# bar easy to miss. Selection highlight uses the theme's full-strength
# SYN_ACCENT (not the dim/muted variant) so the picked row stands out.
CHOSEN_DIR=$(printf '%s\n' \
  "$HOME/Pictures/Screenshots" \
  "$HOME" \
  "$HOME/Videos" \
  "$HOME/Desktop" \
  "$HOME/Downloads" \
  | wmenu -l 5 -f "Terminus 14" -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_ACCENT" -s "$SYN_BG" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "Save screenshot to:")

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

echo "Saved ${out_file##*/}" > "$MARKER"
notify-send "Screenshot" "Saved -> $out_file" 2>/dev/null || true
