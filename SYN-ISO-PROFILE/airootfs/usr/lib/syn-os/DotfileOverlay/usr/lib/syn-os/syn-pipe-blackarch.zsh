#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                       S Y N - P I P E - B L A C K A R C H
#
#   Labwc pipe menu listing the tools syn-blackarch-toggle.zsh installs
#   (set, metasploit, aircrack-ng). Only reached once Enable has inserted
#   this menu's entry into menu.xml — see Preferences > BlackArch.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-BLACKARCH (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

PKGS=(set metasploit aircrack-ng)

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

# The package name isn't always the binary to run — resolve the real
# /usr/bin entry from the package's own file list instead of assuming
# pkg==binary.
tool_item() {
  local pkg="$1" bin
  bin="$(pacman -Ql "$pkg" 2>/dev/null | awk '$2 ~ /\/usr\/bin\// {print $2; exit}')"
  [[ -z "$bin" ]] && return
  local safe_label="$(xml_escape "$pkg")"
  print "  <item label=\"$safe_label\">"
  print "    <action name=\"Execute\"><command>foot -e zsh -c '${bin}; exec zsh'</command></action>"
  print "  </item>"
}

any=0
for pkg in "${PKGS[@]}"; do
  pacman -Qi "$pkg" >/dev/null 2>&1 || continue
  tool_item "$pkg"
  any=1
done

(( any )) || print '<item label="Nothing installed — try Preferences &gt; BlackArch &gt; Enable again"/>'

print '</openbox_pipe_menu>'
