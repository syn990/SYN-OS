#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                   S Y N - B L A C K A R C H - T O G G L E
#
#   Enable/Disable BlackArch, live, on an already-installed system. Enable
#   runs BlackArch's strap.sh then installs set/metasploit/aircrack-ng —
#   not the blackarch-recon/scanner/webapp groups, those drag in
#   badkarma/vega and their broken webkit2gtk/libsoup deps. Inserts the
#   Applications submenu into menu.xml; Disable reverses all of it.
#
#   Launched directly from menu.xml (no foot wrapper) — the rofi picker
#   is already its own centered popup. Once a choice is made, this script
#   re-invokes itself with --enable/--disable inside syn_popup::run, so
#   the actual doas/strap.sh/pacman work gets a real terminal (strap.sh
#   has its own prompts) framed the same way as every other tool.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BLACKARCH-TOGGLE (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

MENU_XML="$HOME/.config/labwc/menu.xml"
PKGS=(set metasploit aircrack-ng)

SELF="/usr/lib/syn-os/syn-blackarch-toggle.zsh"

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
source /usr/lib/syn-os/syn-ui.zsh
source /usr/lib/syn-os/syn-popup-lib.zsh
syn_theme_load

is_enabled() { pacman -Qi set >/dev/null 2>&1; }

insert_menu_block() {
  # Not "BLACKARCH-MENU-START" — the template's own explanatory comment
  # above the anchor contains that exact substring ("BLACKARCH-MENU-
  # START/END: inserted/removed here by..."), so that guard always
  # matched and insert_menu_block silently no-op'd on every real run.
  # blackarch-pipe only exists after a real insertion.
  grep -q 'id="blackarch-pipe"' "$MENU_XML" 2>/dev/null && return
  sed -i '/<!-- BLACKARCH-MENU-END -->/i\
    <!-- BLACKARCH-MENU-START -->\
    <menu id="blackarch-pipe" label="BlackArch" execute="/usr/lib/syn-os/syn-pipe-blackarch.zsh" />' \
    "$MENU_XML"
}

# Range ends at the single-line <menu .../> insert_menu_block wrote — keep
# that entry on one physical line, or this range never closes and eats the
# rest of the file.
remove_menu_block() {
  sed -i '/<!-- BLACKARCH-MENU-START -->/,/<menu id="blackarch-pipe".*\/>/d' "$MENU_XML"
}

enable_blackarch() {
  syn_ui::step "Enabling BlackArch — this needs a real terminal for strap.sh's prompts"
  if ! curl -fsSL https://blackarch.org/strap.sh -o /tmp/strap.sh; then
    syn_ui::error "Couldn't reach blackarch.org."
    notify-send -u critical "BlackArch" "Enable failed: couldn't reach blackarch.org" 2>/dev/null || true
    exit 1
  fi
  syn_ui::doas sh /tmp/strap.sh
  rm -f /tmp/strap.sh

  # strap.sh only runs `pacman -Syy` (refresh repo databases), never a full
  # upgrade. On a system that hasn't been updated since its ISO was built,
  # BlackArch's packages are often built against newer core libraries than
  # what's installed, and pacman fails on unresolvable version conflicts
  # (the classic Arch partial-upgrade trap). Syncing the whole system
  # first avoids that.
  syn_ui::step "Updating system before installing BlackArch packages"
  syn_ui::doas pacman -Syu --noconfirm

  if ! syn_ui::doas pacman -S --noconfirm --needed "${PKGS[@]}"; then
    syn_ui::error "Installing ${PKGS[*]} failed even after a full update."
    syn_ui::error "Check 'sudo pacman -S ${PKGS[*]}' manually for the real dependency error."
    notify-send -u critical "BlackArch" "Enable failed: ${PKGS[*]} install error, see terminal" 2>/dev/null || true
    exit 1
  fi
  insert_menu_block
  syn_ui::step_done "BlackArch enabled. Reload the menu (Super+Escape) to see Applications > BlackArch."
  notify-send "BlackArch" "Enabled — repo synced, ${PKGS[*]} installed. More at blackarch.org" 2>/dev/null || true
}

disable_blackarch() {
  syn_ui::step "Removing ${PKGS[*]} and the [blackarch] repo"
  doas pacman -Rns --noconfirm "${PKGS[@]}" 2>/dev/null || true
  doas sed -i '/\[blackarch\]/{N;d}' /etc/pacman.conf
  remove_menu_block
  syn_ui::step_done "BlackArch disabled. Reload the menu (Super+Escape) to hide Applications > BlackArch."
  notify-send "BlackArch" "Disabled — packages and repo removed" 2>/dev/null || true
}

# Re-invocation entry point: syn_popup::run below calls back into this
# same script with --enable/--disable so the actual work runs inside the
# popup instead of the rofi-picker process (which never gets a foot
# window of its own at all now).
case "${1:-}" in
  --enable)  enable_blackarch;  exit 0 ;;
  --disable) disable_blackarch; exit 0 ;;
esac

# not "status" — that's a read-only zsh builtin
if is_enabled; then
  blackarchStatus="currently enabled"
else
  blackarchStatus="currently disabled"
fi

choice="$(printf '%s\n' "Enable BlackArch" "Disable BlackArch" \
  | syn_pick::rofi "BlackArch (${blackarchStatus}):")"

case "$choice" in
  Enable*)  syn_popup::run zsh "$SELF" --enable ;;
  Disable*) syn_popup::run zsh "$SELF" --disable ;;
  *) exit 0 ;;
esac
