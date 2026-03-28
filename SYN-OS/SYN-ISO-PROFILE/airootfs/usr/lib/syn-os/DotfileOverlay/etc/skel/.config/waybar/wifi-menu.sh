#!/bin/bash

INTERFACE="wlan0"

# 1. Start a scan (quietly)
iwctl station $INTERFACE scan > /dev/null

# 2. Get the list and clean it
# - Strip ANSI colors
# - Skip the 4 lines of header
# - Strip leading characters (*, >, spaces)
# - Keep ONLY the text before the first "big gap" of 2+ spaces
CHOSEN_SSID=$(iwctl station $INTERFACE get-networks | \
    sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | \
    sed '1,4d' | \
    sed -E 's/^[* > ]+//' | \
    sed -E 's/[ ]{2,}.*//' | \
    wmenu -N '#000000' -n '#ffffff' -S '#400101' -s '#ffffff' -M '#260101' -m '#ffffff' -p "WiFi:")

# 3. If you picked something, connect
if [ -n "$CHOSEN_SSID" ]; then
    # Use foot so you can type a password if needed
    foot --title="WiFi-Connect" -e iwctl station $INTERFACE connect "$CHOSEN_SSID"
fi
