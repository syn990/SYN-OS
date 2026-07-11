#!/bin/bash
# SYN-OS power menu: rofi dmenu picker, no wlogout dependency (that's an
# AUR-only package, and pacstrap can't reach the AUR — see docs/waybar.md).
set -euo pipefail

CURRENT_THEME_FILE="$HOME/.config/syn-os/current-theme"
THEMES_DIR="$HOME/.config/syn-os/themes"
theme_name="SYN-OS-RED"
[ -f "$CURRENT_THEME_FILE" ] && theme_name="$(<"$CURRENT_THEME_FILE")"
theme_file="$THEMES_DIR/$theme_name.theme"
[ -f "$theme_file" ] && source "$theme_file"
SYN_BG="${SYN_BG:-#000000}"
SYN_BG_ALT="${SYN_BG_ALT:-#100000}"
SYN_TEXT="${SYN_TEXT:-#ffffff}"
SYN_ACCENT="${SYN_ACCENT:-#800000}"
SYN_BORDER="${SYN_BORDER:-#444444}"

# rofi's default theme is built on root-level variables (background,
# foreground, lightbg, ...), not per-widget properties — overriding only
# `window`/`element selected` (as an earlier version of this script did)
# leaves everything else (entry box, unselected rows, borders) on rofi's
# stock light Solarized palette, which is why it rendered mostly white
# regardless of the active SYN-OS theme.
choice=$(printf 'Lock\nLog Out\nReboot\nPower Off' | rofi -dmenu -p "Power" \
  -location 3 -xoffset -12 -yoffset 34 \
  -theme-str "* { background: ${SYN_BG}e6; background-color: ${SYN_BG}e6; foreground: $SYN_TEXT; lightbg: $SYN_BG_ALT; lightfg: $SYN_TEXT; selected-normal-background: $SYN_ACCENT; selected-normal-foreground: $SYN_TEXT; border-color: $SYN_BORDER; } window { location: northeast; anchor: northeast; border: 1px; }")

case "$choice" in
  Lock)      swaylock -f -c 1a0000 ;;
  "Log Out") pkill labwc ;;
  Reboot)    systemctl reboot ;;
  "Power Off") systemctl poweroff ;;
esac
