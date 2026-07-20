#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                 S Y N - B A R - T O G G L E - P O S I T I O N
#
#   Flips waybar between top and bottom by rewriting config.jsonc's
#   "position" field in place, then signals waybar (SIGUSR2) to reload.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BAR-TOGGLE-POSITION (Waybar)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

waybar_cfg="$HOME/.config/waybar/config.jsonc"
current="$(grep -o '"position":[[:space:]]*"[a-z]*"' "$waybar_cfg" | grep -o '"[a-z]*"$' | tr -d '"')"
next="top"
[[ "${current:-top}" == "top" ]] && next="bottom"

if grep -q '"position"' "$waybar_cfg"; then
  sed -i "s|\"position\":[[:space:]]*\"[a-z]*\"|\"position\": \"$next\"|" "$waybar_cfg"
else
  sed -i "0,/\"height\":/s||\"position\": \"$next\",\n  \"height\":|" "$waybar_cfg"
fi
pkill -SIGUSR2 waybar 2>/dev/null || true
notify-send "Waybar" "Moved to $next" 2>/dev/null || true
