#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                   S Y N - S E R V I C E S - T O G G L E
#
#   Enable/disable a handful of services that are installed but disabled
#   by default (same reasoning as sshd always was: the package is there,
#   nothing starts automatically, until you actually want it). First pick
#   is which service, second is on/off — shows each service's live state
#   in the label so the picker itself is the status display too.
#
#   Launched directly from menu.xml (no foot wrapper) — the rofi pickers
#   are already their own centered popups. The actual doas action runs
#   inside syn_popup::run so its output gets the same framing as
#   everything else, closing itself when it's done.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-SERVICES-TOGGLE (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
source /usr/lib/syn-os/syn-ui.zsh
source /usr/lib/syn-os/syn-popup-lib.zsh
syn_theme_load

typeset -A services
services=(
  "SSH (sshd)"              sshd
  "Bluetooth"                bluetooth
  "QEMU Guest Agent"        qemu-guest-agent
)

state_label() {
  local unit="$1"
  systemctl is-active --quiet "$unit" && echo "running" || echo "stopped"
}

typeset -a menu_lines
for name in "${(@k)services}"; do
  unit="${services[$name]}"
  menu_lines+=("${name} — $(state_label "$unit")")
done

chosen="$(printf '%s\n' "${(@o)menu_lines}" | syn_pick::rofi "Services:")"
[[ -z "$chosen" ]] && exit 0

chosen_name="${chosen% — *}"
unit="${services[$chosen_name]:-}"
if [[ -z "$unit" ]]; then
  syn_ui::error "Unknown selection: $chosen_name"
  exit 1
fi

action="$(printf '%s\n' "Enable + Start" "Disable + Stop" \
  | syn_pick::rofi "${chosen_name} ($(state_label "$unit")):")"

case "$action" in
  Enable*)  verb=enable;  desc="${chosen_name} enabled and started." ;;
  Disable*) verb=disable; desc="${chosen_name} stopped and disabled." ;;
  *) exit 0 ;;
esac

syn_popup::run zsh -c '
  source /usr/lib/syn-os/syn-ui.zsh
  syn_ui::doas systemctl "$1" --now "$2" || exit $?
  syn_ui::step_done "$3"
' -- "$verb" "$unit" "$desc"
