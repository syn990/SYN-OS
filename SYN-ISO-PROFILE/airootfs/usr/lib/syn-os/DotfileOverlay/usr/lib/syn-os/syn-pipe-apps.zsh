#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P I P E - A P P S
#
#   Generates the labwc pipe menu behind "SYN Apps": every installed
#   application whose desktop entry is named SYN-something (SYN-TREK,
#   SYN-BIOGENESIS, SYN-NETJUMPER ...), from ~/.local/share/applications and
#   /usr/share/applications, sorted by name. Nothing is listed by hand, so a
#   host shows exactly the SYN apps built or installed on it. SYN-SHELL is
#   left out (it has its own top-level entry), as is anything NoDisplay.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-APPS (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -uo pipefail

xml_escape() {
  print -r -- "$1" | sed \
    -e 's/&/\&amp;/g' \
    -e 's/"/\&quot;/g' \
    -e "s/'/\&apos;/g" \
    -e 's/</\&lt;/g' \
    -e 's/>/\&gt;/g'
}

# First "Key=" value in the [Desktop Entry] group only.
desktop_value() {
  awk -v k="$2" -F= '
    /^\[/ { inside = ($0 == "[Desktop Entry]") ; next }
    inside && $1 == k { sub(/^[^=]*=/, ""); print; exit }
  ' "$1"
}

typeset -A seen
typeset -a rows
for f in "$HOME"/.local/share/applications/*.desktop(N) /usr/share/applications/*.desktop(N); do
  local id="${f:t}"
  [[ -n "${seen[$id]:-}" ]] && continue   # user copy shadows the system one
  seen[$id]=1
  local name="$(desktop_value "$f" Name)"
  [[ "$name" == SYN-* ]] || continue
  [[ "$name" == "SYN-SHELL" ]] && continue
  [[ "$(desktop_value "$f" NoDisplay)" == "true" ]] && continue
  local exec="$(desktop_value "$f" Exec)"
  exec="${exec// %[fFuUdDnNickvm]/}"     # drop desktop-entry field codes
  [[ -n "$exec" ]] || continue
  rows+=("${name}"$'\t'"${exec}")
done

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'
if (( ${#rows} == 0 )); then
  print '  <item label="No SYN apps installed on this host"/>'
else
  # Byte order on the name alone, so SYN-BIOGENESIS sorts before SYN-BIOGENESIS-3D.
  for row in "${(@f)$(print -rl -- "${rows[@]}" | LC_ALL=C sort -t$'\t' -k1,1)}"; do
    local name="${row%%$'\t'*}" exec="${row#*$'\t'}"
    print "  <item label=\"$(xml_escape "$name")\">"
    print "    <action name=\"Execute\"><command>$(xml_escape "$exec")</command></action>"
    print "  </item>"
  done
fi
print '</openbox_pipe_menu>'
