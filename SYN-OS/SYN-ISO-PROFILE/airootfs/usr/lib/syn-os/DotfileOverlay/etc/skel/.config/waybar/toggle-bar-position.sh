#!/usr/bin/env zsh
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
