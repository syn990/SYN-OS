#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - B A R - W I F I
#
#   Waybar's network module on-click: scans with iwctl, feeds the SSID
#   list into wmenu, then opens a foot terminal to connect (so a
#   passphrase prompt has somewhere to be typed).
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BAR-WIFI (Waybar)
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

# Real `iwctl device list` columns are Name/Address/Powered/Adapter/Mode
# (5 fields) — Mode, the one we actually want to match, is $5, not $4.
# $4=="station" checks the adapter name (e.g. "phy0") and never matches,
# so this always reported "No wireless device found" even with a real
# wifi card present.
INTERFACE=$(iwctl device list 2>/dev/null | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | awk '$5=="station" {print $1; exit}')
if [ -z "$INTERFACE" ]; then
    notify-send "WiFi" "No wireless device found" 2>/dev/null || echo "No wireless device found" >&2
    exit 1
fi

# `station scan` is async over D-Bus — it returns as soon as the scan
# request is accepted, not once results are in. get-networks called
# immediately after can return stale or empty results. iwd's own scans
# typically take a few seconds; give it a moment before reading back.
iwctl station "$INTERFACE" scan > /dev/null
sleep 3

# Strips ANSI colors, the 4-line header, leading list markers, then keeps
# only the text before the first 2+-space gap (SSID column only).
CHOSEN_SSID=$(iwctl station "$INTERFACE" get-networks | \
    sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | \
    sed '1,4d' | \
    sed -E 's/^[* > ]+//' | \
    sed -E 's/[ ]{2,}.*//' | \
    wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "WiFi:")

if [ -n "$CHOSEN_SSID" ]; then
    foot --title="WiFi-Connect" -e iwctl station "$INTERFACE" connect "$CHOSEN_SSID"
fi
