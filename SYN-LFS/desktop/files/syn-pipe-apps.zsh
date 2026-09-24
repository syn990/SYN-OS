#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P I P E - A P P S
#
#   Labwc pipe menu for "All Applications": every installed program's
#   .desktop file (the user's own first, then $XDG_DATA_DIRS), sorted into
#   the freedesktop main categories. SYN-OS's stand-in for SYN-OS-X's
#   archlinux-xdg-menu, so it lists whatever is installed right now.
#   Terminal programs open in foot.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-APPS (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
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

# The same file name in an earlier directory wins, as the spec says
typeset -a dirs files
typeset -A seen
dirs=("${XDG_DATA_HOME:-$HOME/.local/share}/applications"
      ${^${(s.:.)${XDG_DATA_DIRS:-/usr/local/share:/usr/share}}}/applications)
for d in "${dirs[@]}"; do
  for f in "$d"/*.desktop(N); do
    (( ${+seen[${f:t}]} )) && continue
    seen[${f:t}]=1
    files+=("$f")
  done
done

if (( ${#files} == 0 )); then
  print '<item label="No applications found"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

# One "category<TAB>name<TAB>command" line per program that wants to be
# shown. Field codes (%f, %U and so on) are dropped from Exec, since a
# menu click has no files to pass.
entries="$(awk '
  function main_cat(list,    n, i, c, parts) {
    n = split(list, parts, ";")
    for (i = 1; i <= n; i++) {
      c = parts[i]
      if (c == "AudioVideo" || c == "Audio" || c == "Video") return "Multimedia"
      if (c == "Development") return "Development"
      if (c == "Education") return "Education"
      if (c == "Game") return "Games"
      if (c == "Graphics") return "Graphics"
      if (c == "Network") return "Internet"
      if (c == "Office") return "Office"
      if (c == "Science") return "Science"
      if (c == "Settings") return "Settings"
      if (c == "System") return "System"
      if (c == "Utility") return "Accessories"
    }
    return "Other"
  }
  function flush() {
    if (type == "Application" && name != "" && cmd != "" && nodisplay != "true" && hidden != "true") {
      gsub(/%[fFuUdDnNickvm]/, "", cmd)
      gsub(/%%/, "%", cmd)
      sub(/[ \t]+$/, "", cmd)
      if (terminal == "true") cmd = "foot -e " cmd
      print cat "\t" name "\t" cmd
    }
    type = name = cmd = terminal = nodisplay = hidden = ""
    cat = "Other"
  }
  FNR == 1 { flush(); inentry = 0 }
  /^\[/ { inentry = ($0 == "[Desktop Entry]"); next }
  !inentry { next }
  /^Type=/ { type = substr($0, 6) }
  /^Name=/ { name = substr($0, 6) }
  /^Exec=/ { cmd = substr($0, 6) }
  /^Terminal=/ { terminal = substr($0, 10) }
  /^NoDisplay=/ { nodisplay = substr($0, 11) }
  /^Hidden=/ { hidden = substr($0, 8) }
  /^Categories=/ { cat = main_cat(substr($0, 12)) }
  END { flush() }
' "${files[@]}" | sort -t $'\t' -k1,1 -k2,2f)"

current=""
while IFS=$'\t' read -r cat name cmd; do
  [[ -z "$name" ]] && continue
  if [[ "$cat" != "$current" ]]; then
    [[ -n "$current" ]] && print '  </menu>'
    print "  <menu id=\"apps-${cat:l}\" label=\"$(xml_escape "$cat")\">"
    current="$cat"
  fi
  print "    <item label=\"$(xml_escape "$name")\">"
  print "      <action name=\"Execute\"><command>$(xml_escape "$cmd")</command></action>"
  print "    </item>"
done <<< "$entries"
[[ -n "$current" ]] && print '  </menu>'

print '</openbox_pipe_menu>'
