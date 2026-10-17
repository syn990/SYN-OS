#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                           S Y N - P I P E - H U M
#
#   Labwc pipe menu for the acoustic monitor (syn-hum): turn it on or off,
#   pick its style and volume, hear every sound of the style in use, watch
#   the signals it plays, or read what each style plays. The style and
#   volume in use are tagged [CURRENT], the way syn-pipe-display.zsh tags
#   its modes. Choosing one runs `syn-hum set`, which writes
#   ~/.config/syn-os/hum.conf and tells a running monitor at once; the
#   waybar button turns it on and off the same as the first entry here.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-HUM (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -uo pipefail

HUM=/usr/lib/syn-os/syn-hum
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/syn-os/hum.conf"

# What's in use: hum.conf's lines, or syn-hum's own defaults
style=uplink volume=normal
if [[ -r $CONF ]]; then
  while read -r key value _; do
    case $key in
      style) style=$value ;;
      volume) volume=$value ;;
    esac
  done < $CONF
fi

item() {
  print "  <item label=\"$1\"><action name=\"Execute\"><command>$2</command></action></item>"
}

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'

if $HUM status >/dev/null 2>&1; then
  item "Turn Off" "$HUM stop"
else
  item "Turn On" "$HUM start"
fi

print '  <separator label="Style" />'
# The styles, as `syn-hum styles` names them: its unindented lines
for s in ${(f)"$($HUM styles | grep -v '^[[:space:]]' | grep -v '^$')"}; do
  label=$s
  [[ $s == $style ]] && label="$s [CURRENT]"
  item "$label" "$HUM set style $s"
done

print '  <separator label="Volume" />'
for v in quiet normal loud; do
  label=$v
  [[ $v == $volume ]] && label="$v [CURRENT]"
  item "$label" "$HUM set volume $v"
done

print '  <separator />'
item "Hear Every Sound of $style (90 s)" "foot -e $HUM test"
item "Watch the Signals" "foot -e $HUM watch"
item "What Each Style Plays" "foot -e zsh -c '$HUM styles | less'"

print '</openbox_pipe_menu>'
