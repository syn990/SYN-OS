#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P I P E - T H E M E
#
#   Labwc pipe menu listing ~/.config/syn-os/themes/*.theme, grouped by
#   Vanilla/Homage/Neutral. Each entry runs syn-theme-apply <name> to
#   switch live. See docs/theming.md.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-THEME (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail
source /usr/lib/syn-os/syn-theme-lib.zsh

THEMES_DIR="$HOME/.config/syn-os/themes"

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

current="$(syn_theme_current)"

if [[ ! -d "$THEMES_DIR" ]] || ! ls "$THEMES_DIR"/*.theme >/dev/null 2>&1; then
  print '<item label="No themes found in ~/.config/syn-os/themes"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

theme_item() {
  local theme_name="$1" label="$1"
  [[ "$theme_name" == "$current" ]] && label="${theme_name} (active)"
  local safe_label="$(xml_escape "$label")"
  print "    <item label=\"$safe_label\">"
  print "      <action name=\"Execute\"><command>syn-theme-apply ${theme_name}</command></action>"
  print "    </item>"
}

typeset -a vanilla_themes homage_themes neutral_themes
for f in "$THEMES_DIR"/*.theme; do
  ( SYN_THEME_GROUP=""; source "$f" 2>/dev/null
    print -r -- "${SYN_THEME_GROUP:-vanilla}"
  ) | IFS= read -r group
  case "$group" in
    homage)  homage_themes+=("${f:t:r}") ;;
    neutral) neutral_themes+=("${f:t:r}") ;;
    *)       vanilla_themes+=("${f:t:r}") ;;
  esac
done

print '  <menu id="theme-vanilla" label="Vanilla">'
for theme_name in "${vanilla_themes[@]}"; do theme_item "$theme_name"; done
print '  </menu>'

print '  <menu id="theme-homage" label="Homage">'
for theme_name in "${homage_themes[@]}"; do theme_item "$theme_name"; done
print '  </menu>'

print '  <menu id="theme-neutral" label="Neutral">'
for theme_name in "${neutral_themes[@]}"; do theme_item "$theme_name"; done
print '  </menu>'

print '  <separator/>'
print '  <item label="Edit Themes Folder (new theme = copy a .theme file)">'
print '    <action name="Execute"><command>foot -e spf '"$THEMES_DIR"'</command></action>'
print '  </item>'

print '</openbox_pipe_menu>'
