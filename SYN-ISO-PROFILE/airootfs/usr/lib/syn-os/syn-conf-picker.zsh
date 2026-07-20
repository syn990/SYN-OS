#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                       S Y N - C O N F - P I C K E R
#
#   TTY-native editor for /etc/syn-os/synos.conf: fzf-pick a key, type a new
#   value, write it back. Exists because rofi can't run here — the live ISO
#   autologins straight to a plain tty1 shell, no compositor. Replaces
#   freehand nano editing for the common case (nano's still there for
#   anything this doesn't cover, e.g. adding a new key).
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-CONF-PICKER (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

ConfFile="/etc/syn-os/synos.conf"

# fzf's --preview shells out to this same script with --preview-line N —
# prints the contiguous '# ...' comment block directly above line N in
# synos.conf (the same explanation you'd see reading the file in an
# editor) and exits, without touching the picker's own state at all.
if [ "${1:-}" = "--preview-line" ]; then
  target="${2:?}"
  awk -v target="$target" '
    { lines[NR] = $0 }
    END {
      n = target - 1
      out = ""
      while (n >= 1 && lines[n] ~ /^#/) {
        out = lines[n] "\n" out
        n--
      }
      if (out == "") out = "(no comment above this key)\n"
      printf "%s", out
    }
  ' "$ConfFile"
  exit 0
fi

source /usr/lib/syn-os/syn-ui.zsh

if [ ! -w "$ConfFile" ]; then
  syn_ui::error "$ConfFile not writable (run with doas)"
  exit 1
fi

# Loops until the user cancels out of the fzf picker (Esc / empty
# selection) — each pass re-reads $ConfFile fresh so the list and preview
# always reflect the value just set, and picking several settings in a row
# doesn't mean re-running the command each time.
while true; do
  # Only real KEY="value" assignments, not comments or blank lines — same
  # shape synos.conf uses throughout, one assignment per line.
  Lines=("${(@f)$(grep -nE '^[A-Za-z_][A-Za-z0-9_]*="' "$ConfFile")}")
  if [ ${#Lines[@]} -eq 0 ]; then
    syn_ui::error "No KEY=\"value\" lines found in $ConfFile"
    exit 1
  fi

  # fzf shows "N: KEY = value" — N is real line number in $ConfFile, kept in
  # the row so the preview pane can look up that key's comment, but hidden
  # from display via --with-nth so the picker still just reads "KEY = value".
  Picked="$(printf '%s\n' "${Lines[@]}" | sed -E 's/^([0-9]+):([A-Za-z0-9_]+)="([^"]*)"/\1: \2 = \3/' \
    | fzf --prompt="synos.conf key > " --height=~60% --border --header="Enter to edit, Esc when done" \
          --delimiter=': ' --with-nth=2.. \
          --preview="zsh /usr/lib/syn-os/syn-conf-picker.zsh --preview-line {1}" \
          --preview-window=down:6:wrap)"

  if [ -z "$Picked" ]; then
    syn_ui::info "Done editing"
    break
  fi

  Picked="${Picked#*: }"
  Key="${Picked%% = *}"
  CurrentValue="${Picked#* = }"

  syn_ui::info "Editing ${Key} (current: \"${CurrentValue}\")"
  printf "New value: "
  read -r NewValue </dev/tty

  if [ -z "$NewValue" ] && [ "$NewValue" != "$CurrentValue" ]; then
    syn_ui::info "Empty input, nothing changed"
    continue
  fi

  # synos.conf values are always KEY="value" — a literal " in NewValue would
  # close that quote early and spill the rest into the file as raw shell
  # syntax, which syn-config.zsh then sources. Reject rather than try to
  # escape a quote inside an already-quoted shell string.
  case "$NewValue" in
    *'"'*)
      syn_ui::error "Value can't contain a double-quote character (breaks synos.conf's KEY=\"value\" format)"
      continue
      ;;
  esac

  # Escape / and & for sed's replacement side; the search side only ever
  # matches a literal ^Key=" anchor, no user input there.
  EscapedValue="${NewValue//\\/\\\\}"
  EscapedValue="${EscapedValue//\//\\/}"
  EscapedValue="${EscapedValue//&/\\&}"

  sed -i -E "s/^${Key}=\"[^\"]*\"/${Key}=\"${EscapedValue}\"/" "$ConfFile"
  syn_ui::step_done "${Key} set to \"${NewValue}\""
done
