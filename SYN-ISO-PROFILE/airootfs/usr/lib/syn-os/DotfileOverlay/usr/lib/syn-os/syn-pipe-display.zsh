#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                        S Y N - P I P E - D I S P L A Y
#
#   Generates a labwc pipe menu (Openbox XML format) per connected display,
#   with power/persistence/primary/layout/mode/scale/rotation controls
#   built from live wlr-randr output.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-DISPLAY (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

# Outputs the user has chosen to always keep off across reboots — one name
# per line, no pattern-matching (eDP/LVDS/DSI vary by hardware, so this
# only ever holds names picked via this menu). autostart applies the
# "only if something else is on" fallback — see labwc/autostart.
DISABLED_OUTPUTS_FILE="$HOME/.config/syn-os/disabled-outputs"

# Relative layout the user has chosen (e.g. "HDMI-A-1 right-of eDP-1"),
# one relation per line, at most one per output — a fresh pick for an
# output overwrites its old line rather than appending. Without this,
# re-enabling an output after it was off leaves wlr-randr defaulting it
# to 0,0, i.e. cloned onto whatever else is on. autostart replays these
# relations in file order on every login, same pattern as
# DISABLED_OUTPUTS_FILE above.
LAYOUT_FILE="$HOME/.config/syn-os/display-layout"

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'

if ! command -v wlr-randr >/dev/null 2>&1; then
  print '<item label="wlr-randr not found"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

typeset -a rr
rr=("${(@f)$(wlr-randr)}")

# -------- XML Escape --------
xml_escape() {
  print -r -- "$1" | sed \
    -e 's/&/\&amp;/g' \
    -e 's/"/\&quot;/g' \
    -e "s/'/\&apos;/g" \
    -e 's/</\&lt;/g' \
    -e 's/>/\&gt;/g'
}

# -------- Extract Outputs --------
typeset -a outputs
for line in "${rr[@]}"; do
  [[ -z "$line" ]] && continue
  [[ "$line" == " "* ]] && continue
  first="${line%% *}"
  [[ "$first" == "Modes:" || "$first" == "Transform" ]] && continue
  [[ " ${outputs[*]} " == *" $first "* ]] || outputs+=("$first")
done

if (( ${#outputs} == 0 )); then
  print '<item label="No outputs detected"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

# Total currently-enabled outputs — gates "Turn OFF" below so the menu
# itself can never be used to switch off the last screen (autostart's
# apply_persisted_display_state only guards the reboot path; this is the
# equivalent guard for live clicks).
enabled_count="$(awk '/Enabled:/ && $2=="yes"' <<<"${(F)rr}" | wc -l)"

# -------- Helpers --------
get_block() {
  local out="$1"
  awk -v o="$out" '
    $1==o {flag=1; print; next}
    flag && /^[^[:space:]]/ {exit}
    flag {print}
  ' <<<"${(F)rr}"
}

get_modes() {
  local out="$1"
  get_block "$out" |
  awk '
    /^[[:space:]]+[0-9]+x[0-9]+/ {
      # $1=resolution, $2="px,", $3=refresh rate — e.g. "3440x1440 px, 99.997002 Hz"
      mode=$1"@"$3
      gsub("Hz","",mode)
      current = ($0 ~ /current/) ? "yes" : "no"
      print mode "|" current
    }
  '
}

# -------- Build Menu --------
for name in "${outputs[@]}"; do
  safe_name="$(xml_escape "$name")"

  block="$(get_block "$name")"
  enabled_line="$(awk '/Enabled:/ {print $2}' <<<"$block")"

  state="off"
  [[ "$enabled_line" == "yes" ]] && state="on"

  primary="no"
  [[ "$block" == *"primary"* ]] && primary="yes"

  label="${name:u} (${state:u})"
  safe_label="$(xml_escape "$label")"

  print "<menu id=\"${safe_name}-menu\" label=\"$safe_label\">"

  # -------- POWER --------
  print "  <separator label=\" POWER \"/>"
  if [[ "$state" == "off" ]]; then
    print "  <item label=\"Turn ON (AUTO)\">"
    print "    <action name=\"Execute\"><command>wlr-randr --output \"${name}\" --on</command></action>"
    print "    <action name=\"Reconfigure\"/>"
    print "  </item>"
  elif (( enabled_count > 1 )); then
    print "  <item label=\"Turn OFF\">"
    print "    <action name=\"Execute\"><command>wlr-randr --output \"${name}\" --off</command></action>"
    print "    <action name=\"Reconfigure\"/>"
    print "  </item>"
  else
    print "  <item label=\"Turn OFF (disabled — last screen on)\"/>"
  fi

  # -------- PERSISTENCE (see labwc/autostart's fallback: this output only
  # stays off if another one is on, never leaving a black screen) --------
  is_persisted="no"
  if [[ -f "$DISABLED_OUTPUTS_FILE" ]] && grep -qxF "$name" "$DISABLED_OUTPUTS_FILE" 2>/dev/null; then
    is_persisted="yes"
  fi
  print "  <separator label=\" PERSISTENCE \"/>"
  if [[ "$is_persisted" == "yes" ]]; then
    print "  <item label=\"Always OFF at boot (ON) — click to clear\">"
    print "    <action name=\"Execute\"><command>sh -c 'grep -vxF \"${name}\" \"$DISABLED_OUTPUTS_FILE\" > \"$DISABLED_OUTPUTS_FILE.tmp\" 2>/dev/null; mv \"$DISABLED_OUTPUTS_FILE.tmp\" \"$DISABLED_OUTPUTS_FILE\"'</command></action>"
    print "  </item>"
  else
    print "  <item label=\"Always OFF at boot (set)\">"
    print "    <action name=\"Execute\"><command>sh -c 'mkdir -p \"\${HOME}/.config/syn-os\"; echo \"${name}\" >> \"$DISABLED_OUTPUTS_FILE\"'</command></action>"
    print "  </item>"
  fi

  # -------- PRIMARY --------
  if [[ "$state" == "on" && "$primary" == "no" ]]; then
    print "  <separator label=\" PRIMARY \"/>"
    print "  <item label=\"Set as PRIMARY\">"
    print "    <action name=\"Execute\"><command>wlr-randr --output \"${name}\" --primary</command></action>"
    print "  </item>"
  fi

  # -------- LAYOUT (relative position vs. every other enabled output;
  # see LAYOUT_FILE comment above for why this exists — turning a cloned
  # output back on otherwise leaves it stacked at 0,0) --------
  if [[ "$state" == "on" && ${#outputs} -gt 1 ]]; then
    print "  <separator label=\" LAYOUT \"/>"
    for other in "${outputs[@]}"; do
      [[ "$other" == "$name" ]] && continue
      other_block="$(get_block "$other")"
      other_enabled="$(awk '/Enabled:/ {print $2}' <<<"$other_block")"
      [[ "$other_enabled" != "yes" ]] && continue
      safe_other="$(xml_escape "$other")"

      for rel in left-of right-of above below; do
        rel_label="${rel//-/ }"
        print "  <item label=\"${rel_label:u} ${safe_other}\">"
        print "    <action name=\"Execute\"><command>sh -c 'wlr-randr --output \"${name}\" --${rel} \"${other}\"; mkdir -p \"\${HOME}/.config/syn-os\"; grep -v \"^${name} \" \"$LAYOUT_FILE\" > \"$LAYOUT_FILE.tmp\" 2>/dev/null; mv \"$LAYOUT_FILE.tmp\" \"$LAYOUT_FILE\"; echo \"${name} ${rel} ${other}\" >> \"$LAYOUT_FILE\"'</command></action>"
        print "    <action name=\"Reconfigure\"/>"
        print "  </item>"
      done
    done
  fi

  # -------- MODES --------
  typeset -a modes
  modes=("${(@f)$(get_modes "$name")}")
  if (( ${#modes} > 0 )); then
    print "  <separator label=\" MODES \"/>"
    for m in "${modes[@]}"; do
      mode="${m%%|*}"
      current="${m##*|}"
      res="${mode%@*}"
      rate="${mode#*@}"

      if [[ "$current" == "yes" ]]; then
        print "  <item label=\"${res} @ ${rate}Hz (CURRENT)\"/>"
      else
        print "  <item label=\"${res} @ ${rate}Hz\">"
        print "    <action name=\"Execute\"><command>wlr-randr --output \"${name}\" --on --mode ${res}@${rate}</command></action>"
        print "  </item>"
      fi
    done
  fi

  # -------- SCALE --------
  print "  <separator label=\" SCALE \"/>"
  for s in 1.0 1.25 1.5 2.0; do
    print "  <item label=\"Scale ${s}\">"
    print "    <action name=\"Execute\"><command>wlr-randr --output \"${name}\" --on --scale ${s}</command></action>"
    print "  </item>"
  done

  # -------- ROTATION --------
  print "  <separator label=\" ROTATION \"/>"
  for r in normal 90 180 270; do
    print "  <item label=\"Rotate ${r}\">"
    print "    <action name=\"Execute\"><command>wlr-randr --output \"${name}\" --on --transform ${r}</command></action>"
    print "  </item>"
  done

  print "</menu>"
done

print '</openbox_pipe_menu>'
