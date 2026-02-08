#!/usr/bin/env bash
# Day/Night aware Waybar launcher for SYN‑OS
# Day   (07:00–19:00): bright red box + ▶
# Night (19:00–07:00): dark red box + 

# Configurable hours (24h). Adjust if you like.
DAY_START_HOUR=7
NIGHT_START_HOUR=19

ICON_DAY="▶"    # clean launcher glyph
ICON_NIGHT=""  # terminal (Font Awesome); fallback to ">_" if FA missing

# Fallback if Font Awesome isn't available
if ! fc-list | grep -qi "Font Awesome"; then
  ICON_NIGHT=">_"
fi

now_h=$(date +%H)

in_day=false
if [ "$now_h" -ge "$DAY_START_HOUR" ] && [ "$now_h" -lt "$NIGHT_START_HOUR" ]; then
  in_day=true
fi

if $in_day; then
  # Use CSS class 'syn-day'
  printf '{"text":"%s","class":"syn-day"}\n' "$ICON_DAY"
else
  # Use CSS class 'syn-night'
  printf '{"text":"%s","class":"syn-night"}\n' "$ICON_NIGHT"
fi