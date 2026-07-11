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

INTERFACE=$(iwctl device list 2>/dev/null | awk '$4=="station" {print $1; exit}')
if [ -z "$INTERFACE" ]; then
    notify-send "WiFi" "No wireless device found" 2>/dev/null || echo "No wireless device found" >&2
    exit 1
fi

# 1. Start a scan (quietly)
iwctl station "$INTERFACE" scan > /dev/null

# 2. Get the list and clean it
# - Strip ANSI colors
# - Skip the 4 lines of header
# - Strip leading characters (*, >, spaces)
# - Keep ONLY the text before the first "big gap" of 2+ spaces
CHOSEN_SSID=$(iwctl station "$INTERFACE" get-networks | \
    sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | \
    sed '1,4d' | \
    sed -E 's/^[* > ]+//' | \
    sed -E 's/[ ]{2,}.*//' | \
    wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "WiFi:")

# 3. If you picked something, connect
if [ -n "$CHOSEN_SSID" ]; then
    # Use foot so you can type a password if needed
    foot --title="WiFi-Connect" -e iwctl station "$INTERFACE" connect "$CHOSEN_SSID"
fi
