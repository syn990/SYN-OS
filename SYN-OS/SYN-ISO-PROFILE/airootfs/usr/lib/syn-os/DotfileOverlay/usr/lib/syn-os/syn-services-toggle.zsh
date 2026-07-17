#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                   S Y N - S E R V I C E S - T O G G L E
#
#   Enable/disable any real systemd service on the box, not a hardcoded
#   shortlist — this used to offer exactly 3 units (sshd, bluetooth,
#   qemu-guest-agent) regardless of what was actually installed, so e.g.
#   postgresql/libvirtd/ollama being enabled on a real machine was
#   invisible to it. Now lists every enabled/disabled unit from systemctl
#   itself, labelled with its live state, and only offers the one action
#   that state actually allows (no "Disable" on something already
#   disabled). "static"/"alias"/"indirect" units are left out — those
#   activate as dependencies and can't be enabled/disabled directly, so
#   listing them would just be dead entries that error when picked.
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

# name<TAB>enabled-state<TAB>active-state, one real toggleable unit per
# line. $2 is systemctl's own "enabled"/"disabled" (what we act on); $3 is
# "active"/"inactive" (just shown, not acted on) — a unit can be enabled
# but not currently running, or vice versa for a few oddball units.
unit_lines="$(systemctl list-unit-files --type=service --no-legend \
  | awk '$2=="enabled" || $2=="disabled" {print $1, $2}' \
  | while read -r unit state; do
      name="${unit%.service}"
      active="$(systemctl is-active "$unit" 2>/dev/null || true)"
      printf '%s\t%s\t%s\n' "$name" "$state" "${active:-inactive}"
    done)"

[[ -z "$unit_lines" ]] && exit 0

chosen="$(printf '%s\n' "$unit_lines" \
  | awk -F'\t' '{printf "%s — %s, %s\n", $1, $2, $3}' \
  | syn_pick::rofi "Services:")"
[[ -z "$chosen" ]] && exit 0

chosen_name="${chosen% — *}"
line="$(printf '%s\n' "$unit_lines" | awk -F'\t' -v n="$chosen_name" '$1==n')"
[[ -z "$line" ]] && { syn_ui::error "Unknown selection: $chosen_name"; exit 1; }

unit="${chosen_name}.service"
enabled_state="$(printf '%s' "$line" | awk -F'\t' '{print $2}')"

# Only the action that unit's current state actually allows — no dead
# "Disable" entry on something already disabled.
if [[ "$enabled_state" == "enabled" ]]; then
  action_label="Disable + Stop ${chosen_name}"
  verb=disable; desc="${chosen_name} stopped and disabled."
else
  action_label="Enable + Start ${chosen_name}"
  verb=enable; desc="${chosen_name} enabled and started."
fi

confirmed="$(printf '%s\n' "$action_label" "Cancel" | syn_pick::rofi "Confirm:")"
[[ "$confirmed" == "$action_label" ]] || exit 0

syn_popup::run zsh -c '
  source /usr/lib/syn-os/syn-ui.zsh
  if ! syn_ui::doas systemctl "$1" --now "$2"; then
    rc=$?
    notify-send -u critical "Services" "Failed: $2 ($1)" 2>/dev/null || true
    exit $rc
  fi
  syn_ui::step_done "$3"
  notify-send "Services" "$3" 2>/dev/null || true
' -- "$verb" "$unit" "$desc"
