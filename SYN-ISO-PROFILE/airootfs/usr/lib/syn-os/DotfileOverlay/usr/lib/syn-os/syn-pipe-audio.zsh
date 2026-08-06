#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P I P E - A U D I O
#
#   Generates a labwc pipe menu (Openbox XML format) listing every audio
#   output/input via syn-audio's own CLI (real libpulse, not pactl text
#   parsing) — set-default, mute/unmute, and volume +5/-5/set-to per device,
#   with live state (default/muted/volume%) shown right in the label, same
#   "(CURRENT)"-style convention as syn-pipe-display.zsh.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-AUDIO (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'

if ! command -v /usr/lib/syn-os/syn-audio >/dev/null 2>&1; then
  print '<item label="syn-audio not found"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

xml_escape() {
  print -r -- "$1" | sed \
    -e 's/&/\&amp;/g' \
    -e 's/"/\&quot;/g' \
    -e "s/'/\&apos;/g" \
    -e 's/</\&lt;/g' \
    -e 's/>/\&gt;/g'
}

# Builds one <menu> per device from a tab-separated "idx name description
# default muted volume" line (syn-audio --list-sinks/-sources's own
# output shape — default/muted are each "default"/"muted" or "-",
# independent since a device can be both at once; volume already
# includes its own trailing '%', e.g. "85%") — $1 is "sink" or "source"
# (selects which --set-default-*/--mute-*/--adjust-*-volume flags to
# emit).
emit_device_menu() {
  local kind="$1" idx name desc is_default is_muted vol
  while IFS=$'\t' read -r idx name desc is_default is_muted vol; do
    [[ -z "$name" ]] && continue
    local tag=""
    [[ "$is_default" == "default" ]] && tag="${tag} [DEFAULT]"
    [[ "$is_muted" == "muted" ]] && tag="${tag} [MUTED]"
    local safe_label
    safe_label="$(xml_escape "${desc} (${vol})${tag}")"
    local safe_id
    safe_id="$(xml_escape "$name")"

    print "  <menu id=\"${kind}-${idx}\" label=\"$safe_label\">"

    if [[ "$is_default" != "default" ]]; then
      print "    <item label=\"Set Default\"><action name=\"Execute\"><command>/usr/lib/syn-os/syn-audio --set-default-${kind} \"${name}\"</command></action></item>"
    fi

    local mute_label="Mute"
    [[ "$is_muted" == "muted" ]] && mute_label="Unmute"
    print "    <item label=\"${mute_label}\"><action name=\"Execute\"><command>/usr/lib/syn-os/syn-audio --mute-${kind} \"${name}\" toggle</command></action></item>"

    print "    <item label=\"Volume +5\"><action name=\"Execute\"><command>/usr/lib/syn-os/syn-audio --adjust-${kind}-volume \"${name}\" +5</command></action></item>"
    print "    <item label=\"Volume -5\"><action name=\"Execute\"><command>/usr/lib/syn-os/syn-audio --adjust-${kind}-volume \"${name}\" -5</command></action></item>"
    print "    <item label=\"Set to 50%\"><action name=\"Execute\"><command>/usr/lib/syn-os/syn-audio --set-${kind}-volume \"${name}\" 50</command></action></item>"
    print "    <item label=\"Set to 100%\"><action name=\"Execute\"><command>/usr/lib/syn-os/syn-audio --set-${kind}-volume \"${name}\" 100</command></action></item>"

    print "  </menu>"
  done
}

print '<menu id="audio-output-menu" label="AUDIO OUTPUTS">'
/usr/lib/syn-os/syn-audio --list-sinks 2>/dev/null | emit_device_menu sink
print '</menu>'

print '<menu id="audio-input-menu" label="AUDIO INPUTS">'
/usr/lib/syn-os/syn-audio --list-sources 2>/dev/null | emit_device_menu source
print '</menu>'

print '<item label="Advanced Settings (syn-audio)">'
print '  <action name="Execute"><command>foot -e /usr/lib/syn-os/syn-audio</command></action>'
print '</item>'

print '</openbox_pipe_menu>'
