#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P I P E - T H E M E
#
#   Labwc pipe menu listing ~/.config/syn-os/themes/*.theme, nested
#   Dark/Light > Vanilla/Flatline/Slab/Halo/Bevel > individual palette.
#   Each leaf entry runs syn-theme-apply <name> to switch live.
#   See docs/theming.md.
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
  local theme_name="$1" label="$2"
  [[ "$theme_name" == "$current" ]] && label="${label} (active)"
  local safe_label="$(xml_escape "$label")"
  print "        <item label=\"$safe_label\">"
  print "          <action name=\"Execute\"><command>syn-theme-apply ${theme_name}</command></action>"
  print "        </item>"
}

typeset -a family_order
family_order=(SYN-OS-VANILLA SYN-OS-FLATLINE SYN-OS-SLAB SYN-OS-HALO SYN-OS-BEVEL)
typeset -A family_labels
family_labels=(
  SYN-OS-VANILLA  Vanilla
  SYN-OS-FLATLINE Flatline
  SYN-OS-SLAB     Slab
  SYN-OS-HALO     Halo
  SYN-OS-BEVEL    Bevel
)

# themes_by_mode_family[$mode:$family] is a newline-separated list of
# "theme_name|label" pairs. zsh associative arrays can't nest, so a flat
# "mode:family" key is the simplest way to bucket in a single pass.
typeset -A themes_by_mode_family
for f in "$THEMES_DIR"/*.theme; do
  theme_name="${f:t:r}"
  ( SYN_THEME_MODE=""; SYN_THEME_FAMILY=""; SYN_THEME_NAME=""
    source "$f" 2>/dev/null
    print -r -- "${SYN_THEME_MODE:-dark}|${SYN_THEME_FAMILY:-SYN-OS-VANILLA}|${SYN_THEME_NAME:-$theme_name}"
  ) | IFS='|' read -r mode family display_name

  # Strip the family/mode prefix so the menu label is just the palette's
  # own name, e.g. SYN-OS-FLATLINE-LIGHT-SKY -> "Sky".
  label="${display_name#$family-}"
  label="${label#LIGHT-}"
  label="${label#DARK-}"
  [[ "$label" == "$display_name" ]] && label="${display_name#SYN-OS-}"
  label="${label:l}"
  label="${label//-/ }"
  label="${(C)label}"

  key="${mode}:${family}"
  themes_by_mode_family[$key]="${themes_by_mode_family[$key]:-}${theme_name}|${label}"$'\n'
done

build_family_menu() {
  local mode="$1" family="$2"
  local key="${mode}:${family}"
  local entries="${themes_by_mode_family[$key]:-}"
  [[ -z "$entries" ]] && return

  print "      <menu id=\"theme-${mode}-${family}\" label=\"$(xml_escape "${family_labels[$family]}")\">"
  local line theme_name label
  while IFS= read -r line; do
    [[ -z "$line" ]] && continue
    theme_name="${line%%|*}"
    label="${line#*|}"
    theme_item "$theme_name" "$label"
  done <<< "$entries"
  print '      </menu>'
}

for mode in dark light; do
  mode_label="Dark"
  [[ "$mode" == "light" ]] && mode_label="Light"
  print "    <menu id=\"theme-${mode}\" label=\"${mode_label}\">"
  for family in "${family_order[@]}"; do
    build_family_menu "$mode" "$family"
  done
  print '    </menu>'
done

print '  <separator/>'
print '  <item label="Edit Themes Folder (new theme = copy a .theme file)">'
print '    <action name="Execute"><command>syn-filemanager '"$THEMES_DIR"'</command></action>'
print '  </item>'

print '</openbox_pipe_menu>'
