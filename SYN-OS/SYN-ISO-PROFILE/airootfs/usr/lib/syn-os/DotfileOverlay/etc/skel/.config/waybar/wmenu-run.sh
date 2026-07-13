#!/bin/bash
set -euo pipefail

CURRENT_THEME_FILE="$HOME/.config/syn-os/current-theme"
THEMES_DIR="$HOME/.config/syn-os/themes"
theme_name="SYN-OS-RED"
[ -f "$CURRENT_THEME_FILE" ] && theme_name="$(<"$CURRENT_THEME_FILE")"
theme_file="$THEMES_DIR/$theme_name.theme"
[ -f "$theme_file" ] && source "$theme_file"
SYN_BG="${SYN_BG:-#000000}"
SYN_TEXT="${SYN_TEXT:-#ffffff}"
SYN_PANEL_HOVER="${SYN_PANEL_HOVER:-#400101}"
SYN_ACCENT_DIM="${SYN_ACCENT_DIM:-#260101}"

waybar_cfg="$HOME/.config/waybar/config.jsonc"
position="top"
if [ -f "$waybar_cfg" ]; then
  match="$(grep -o '"position":[[:space:]]*"[a-z]*"' "$waybar_cfg" | grep -o '"[a-z]*"$' | tr -d '"')"
  [ -n "$match" ] && position="$match"
fi

bottom_flag=()
[ "$position" = "bottom" ] && bottom_flag=(-b)

exec wmenu-run "${bottom_flag[@]}" -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT"
