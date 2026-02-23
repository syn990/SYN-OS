#!/usr/bin/env bash

# How often to change the glyph (seconds)
INTERVAL=20

# A list of cool computery symbols
GLYPHS=(
  "☯" "⚛" "⚙" "⚠" "☢" "☣"
  "♞" "♜" "♟" "♚"
  "★" "✦" "✧" "✩" "✪"
  "✓" "✔" "✗" "✘"
  "⚀" "⚁" "⚂" "⚃" "⚄" "⚅"
  "♠" "♥" "♣" "♦"
  "✉" "✂" "▶" ""
)

# Persist index between runs (Waybar calls scripts repeatedly)
STATE_FILE="/tmp/waybar-glyph-index"

# Load previous index or start at 0
if [ -f "$STATE_FILE" ]; then
  idx=$(cat "$STATE_FILE")
else
  idx=0
fi

# Wrap index
count=${#GLYPHS[@]}
idx=$(( (idx + 1) % count ))

# Save new index
echo "$idx" > "$STATE_FILE"

# Output JSON for Waybar
printf '{"text":"%s"}\n' "${GLYPHS[$idx]}"

# Sleep so the module doesn't hammer CPU
sleep "$INTERVAL"
