#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                   S Y N - S E R V I C E S - T O G G L E
#
#   SYN-LFS's runit take on the Arch syn-services-toggle.zsh (which drives
#   systemctl): every service in /etc/sv, shown as enabled when it's
#   linked into /var/service. Enabling links it in (runsvdir starts it
#   within a few seconds); disabling stops it and removes the link. The
#   gettys and udevd aren't offered, as switching those off from a menu
#   leaves a machine you can't log in to or that finds no devices.
#
#   Same rofi pickers and popup as the Arch version; the doas step runs
#   inside syn_popup::run.
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

# name<TAB>enabled|disabled, one per service
unit_lines="$(for d in /etc/sv/*(N/); do
  name="${d:t}"
  [[ "$name" == getty-* || "$name" == udevd ]] && continue
  if [[ -e "/var/service/$name" ]]; then
    printf '%s\t%s\n' "$name" enabled
  else
    printf '%s\t%s\n' "$name" disabled
  fi
done)"

[[ -z "$unit_lines" ]] && exit 0

chosen="$(printf '%s\n' "$unit_lines" \
  | awk -F'\t' '{printf "%s — %s\n", $1, $2}' \
  | syn_pick::rofi "Services:" -l 15 -theme-str "window { width: 720px; }")"
[[ -z "$chosen" ]] && exit 0

chosen_name="${chosen% — *}"
line="$(printf '%s\n' "$unit_lines" | awk -F'\t' -v n="$chosen_name" '$1==n')"
[[ -z "$line" ]] && { syn_ui::error "Unknown selection: $chosen_name"; exit 1; }
enabled_state="${line#*$'\t'}"

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
  if [[ "$1" == enable ]]; then
    cmd="ln -sfn /etc/sv/$2 /var/service/$2"
  else
    cmd="sv down /var/service/$2; rm -f /var/service/$2"
  fi
  syn_ui::doas sh -c "$cmd" || {
    rc=$?
    notify-send -u critical "Services" "Failed: $2 ($1)" 2>/dev/null || true
    exit $rc
  }
  syn_ui::step_done "$3"
  notify-send "Services" "$3" 2>/dev/null || true
' -- "$verb" "$chosen_name" "$desc"
