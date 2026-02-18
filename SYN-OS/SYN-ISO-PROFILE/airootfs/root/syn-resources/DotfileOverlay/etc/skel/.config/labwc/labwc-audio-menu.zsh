#!/usr/bin/env zsh
# SYN-OS Advanced Dynamic Audio Menu
# Labwc / Openbox pipe menu using pactl
# Shell: zsh
set -euo pipefail

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'

xml_escape() {
  print -r -- "$1" | sed \
    -e 's/&/\&amp;/g' \
    -e 's/"/\&quot;/g' \
    -e "s/'/\&apos;/g" \
    -e 's/</\&lt;/g' \
    -e 's/>/\&gt;/g'
}

# -------- AUDIO OUTPUTS --------
print '<menu id="audio-output-menu" label="AUDIO OUTPUTS">'
while read -r idx name rest; do
  safe_desc=$(xml_escape "$name")
  print "  <menu id=\"sink-$idx\" label=\"$safe_desc\">"
  print "    <item label=\"Set Default\"><action name=\"Execute\"><command>pactl set-default-sink $name</command></action></item>"
  print "    <item label=\"Mute/Unmute\"><action name=\"Execute\"><command>pactl set-sink-mute $name toggle</command></action></item>"
  print "  </menu>"
done < <(pactl list short sinks)
print '</menu>'

# -------- AUDIO INPUTS --------
print '<menu id="audio-input-menu" label="AUDIO INPUTS">'
while read -r idx name rest; do
  safe_desc=$(xml_escape "$name")
  print "  <menu id=\"source-$idx\" label=\"$safe_desc\">"
  print "    <item label=\"Set Default\"><action name=\"Execute\"><command>pactl set-default-source $name</command></action></item>"
  print "    <item label=\"Mute/Unmute\"><action name=\"Execute\"><command>pactl set-source-mute $name toggle</command></action></item>"
  print "  </menu>"
done < <(pactl list short sources)
print '</menu>'

# -------- QUICK PAVUCONTROL --------
print '<item label="Advanced Settings (pavucontrol)">'
print '  <action name="Execute"><command>pavucontrol-qt</command></action>'
print '</item>'

print '</openbox_pipe_menu>'
