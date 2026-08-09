#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                        S Y N - P I P E - R E M O T E
#
#   Labwc pipe menu listing ~/.ssh/config's Host entries as the known
#   machines for syn-relay --stream-app. Each host is its own nested pipe
#   menu (syn-pipe-remote-apps.zsh <host>) so its app list is only
#   fetched when that submenu is actually opened, not up front.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-REMOTE (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

REMOTE_APPS="/usr/lib/syn-os/syn-pipe-remote-apps.zsh"
SSH_CONFIG="$HOME/.ssh/config"

xml_escape() {
  print -r -- "$1" | sed \
    -e 's/&/\&amp;/g' \
    -e 's/"/\&quot;/g' \
    -e "s/'/\&apos;/g" \
    -e 's/</\&lt;/g' \
    -e 's/>/\&gt;/g'
}

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'

if [[ ! -r "$SSH_CONFIG" ]]; then
  print '  <item label="No ~/.ssh/config found"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

# Skips wildcard patterns (Host *, Host foo*) — not a single machine.
typeset -a hosts
hosts=("${(@f)$(grep -i '^[[:space:]]*Host[[:space:]]' "$SSH_CONFIG" \
  | sed -E 's/^[[:space:]]*[Hh]ost[[:space:]]+//' \
  | tr -s ' ' '\n' \
  | grep -v '[*?]' \
  | sort -u)}")

if [[ ${#hosts[@]} -eq 0 ]]; then
  print '  <item label="No Host entries in ~/.ssh/config"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

for host in "${hosts[@]}"; do
  [[ -z "$host" ]] && continue
  safe_host="$(xml_escape "$host")"
  print "  <menu id=\"syn-remote-${host}\" label=\"${safe_host}\" execute=\"${REMOTE_APPS} ${host}\" />"
done

print '</openbox_pipe_menu>'
