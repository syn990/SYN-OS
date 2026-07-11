#!/usr/bin/env zsh
# SYN-OS Dynamic Theme Menu
# Labwc pipe menu listing ~/.config/syn-os/themes/*.theme, each entry
# runs syn-theme-apply <name> to switch live. See docs/theming.md.
set -euo pipefail

THEMES_DIR="$HOME/.config/syn-os/themes"
CURRENT_FILE="$HOME/.config/syn-os/current-theme"

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

current=""
[[ -f "$CURRENT_FILE" ]] && current="$(<"$CURRENT_FILE")"

if [[ ! -d "$THEMES_DIR" ]] || ! ls "$THEMES_DIR"/*.theme >/dev/null 2>&1; then
  print '<item label="No themes found in ~/.config/syn-os/themes"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

for f in "$THEMES_DIR"/*.theme; do
  theme_name="${f:t:r}"
  label="$theme_name"
  [[ "$theme_name" == "$current" ]] && label="${theme_name} (active)"
  safe_label="$(xml_escape "$label")"
  print "  <item label=\"$safe_label\">"
  print "    <action name=\"Execute\"><command>syn-theme-apply ${theme_name}</command></action>"
  print "  </item>"
done

print '  <separator/>'
print '  <item label="Edit Themes Folder (new theme = copy a .theme file)">'
print '    <action name="Execute"><command>foot -e spf '"$THEMES_DIR"'</command></action>'
print '  </item>'

print '</openbox_pipe_menu>'
