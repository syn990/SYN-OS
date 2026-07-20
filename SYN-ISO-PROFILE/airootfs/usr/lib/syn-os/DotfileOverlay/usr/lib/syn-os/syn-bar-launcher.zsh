#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                       S Y N - B A R - L A U N C H E R
#
#   SYN-OS's app launcher: themed wmenu-run, docked to whichever edge
#   waybar itself is currently on (reads config.jsonc's position).
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BAR-LAUNCHER (Waybar)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

source /usr/lib/syn-os/syn-theme-lib.zsh
syn_theme_load
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
