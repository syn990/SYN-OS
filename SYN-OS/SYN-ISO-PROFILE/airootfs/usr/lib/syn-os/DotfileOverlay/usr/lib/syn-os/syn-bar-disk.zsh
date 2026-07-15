#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - B A R - D I S K
#
#   Waybar custom/disk module: root filesystem usage as JSON (text,
#   tooltip listing every real mount, class, percentage), polled every
#   60s per config.jsonc.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BAR-DISK (Waybar)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

root_line=$(df -h / | awk 'NR==2')
root_used=$(awk '{print $3}' <<<"$root_line")
root_size=$(awk '{print $2}' <<<"$root_line")
root_pct=$(awk '{print $5}' <<<"$root_line" | tr -d '%')

class="normal"
[ "$root_pct" -ge 90 ] && class="critical"
[ "$root_pct" -ge 75 ] && [ "$root_pct" -lt 90 ] && class="warning"

tooltip=$(df -h -x tmpfs -x devtmpfs -x squashfs -x overlay 2>/dev/null \
  | awk 'NR==1{next} {printf "%-20s %6s / %-6s (%s)\n", $6, $3, $2, $5}')

python3 -c '
import json, sys
print(json.dumps({
  "text": f"{sys.argv[1]}/{sys.argv[2]}",
  "tooltip": sys.argv[3],
  "class": sys.argv[4],
  "percentage": int(sys.argv[5]),
}))
' "$root_used" "$root_size" "$tooltip" "$class" "$root_pct"
