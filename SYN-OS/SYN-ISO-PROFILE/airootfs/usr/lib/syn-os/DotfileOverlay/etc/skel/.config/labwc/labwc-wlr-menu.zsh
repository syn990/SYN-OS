#!/usr/bin/env zsh
# SYN-OS Advanced Dynamic Display Menu
# Labwc / Openbox pipe menu using wlr-randr
# Shell: zsh

set -euo pipefail

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'

# Ensure wlr-randr exists
if ! command -v wlr-randr >/dev/null 2>&1; then
  print '<item label="wlr-randr not found"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

# Capture output once
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
      # Remove "px, " and "Hz" text
      gsub(" px,","",$1)
      mode=$1"@"$2
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
  else
    print "  <item label=\"Turn OFF\">"
    print "    <action name=\"Execute\"><command>wlr-randr --output \"${name}\" --off</command></action>"
    print "    <action name=\"Reconfigure\"/>"
    print "  </item>"
  fi

  # -------- PRIMARY --------
  if [[ "$state" == "on" && "$primary" == "no" ]]; then
    print "  <separator label=\" PRIMARY \"/>"
    print "  <item label=\"Set as PRIMARY\">"
    print "    <action name=\"Execute\"><command>wlr-randr --output \"${name}\" --primary</command></action>"
    print "  </item>"
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
