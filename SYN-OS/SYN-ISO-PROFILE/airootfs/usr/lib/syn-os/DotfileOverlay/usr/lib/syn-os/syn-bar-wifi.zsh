#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - B A R - W I F I
#
#   Waybar's network module on-click: scans with iwctl, feeds the SSID
#   list into rofi (same centered-card picker as every other menu in
#   SYN-OS, not wmenu's bar-anchored strip), then connects inside the
#   same undecorated syn-os-popup every other secret prompt uses.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BAR-WIFI (Waybar)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
source /usr/lib/syn-os/syn-popup-lib.zsh
syn_theme_load

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
# The click has no other feedback until the picker opens, so toast the
# wait itself — otherwise a 3s pause after clicking reads as "did nothing".
notify-send "WiFi" "Scanning for networks…" 2>/dev/null || true
iwctl station "$INTERFACE" scan > /dev/null
sleep 3

# Strips ANSI colors, the 4-line header, leading list markers, then keeps
# only the text before the first 2+-space gap (SSID column only).
CHOSEN_SSID=$(iwctl station "$INTERFACE" get-networks | \
    sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | \
    sed '1,4d' | \
    sed -E 's/^[* > ]+//' | \
    sed -E 's/[ ]{2,}.*//' | \
    syn_pick::rofi "WiFi:" -l 15 -theme-str "window { width: 720px; }")

if [ -n "$CHOSEN_SSID" ]; then
    syn_popup::run zsh -c '
      iwctl station "$1" connect "$2"
      rc=$?
      if (( rc == 0 )); then
        notify-send "WiFi" "Connected to $2" 2>/dev/null || true
      else
        notify-send -u critical "WiFi" "Failed to connect to $2" 2>/dev/null || true
      fi
      exit $rc
    ' -- "$INTERFACE" "$CHOSEN_SSID"
fi
