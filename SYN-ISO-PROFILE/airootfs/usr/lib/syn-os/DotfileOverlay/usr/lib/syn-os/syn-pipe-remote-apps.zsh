#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                   S Y N - P I P E - R E M O T E - A P P S
#
#   syn-pipe-remote.zsh's nested submenu for one host — thin passthrough,
#   syn-relay --list-apps-for already emits pipe-menu XML wired to
#   --stream-app when given a host.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-REMOTE-APPS (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

RELAY="/usr/lib/syn-os/syn-relay"
host="${1:?usage: syn-pipe-remote-apps.zsh <ssh-host>}"

menu_xml="$("$RELAY" --list-apps-for "$host" 2>/dev/null)" || menu_xml=""

if [[ -z "$menu_xml" ]]; then
  print '<?xml version="1.0" encoding="UTF-8"?>'
  print '<openbox_pipe_menu>'
  print "  <item label=\"${host} unreachable\"/>"
  print '</openbox_pipe_menu>'
  exit 0
fi

print -r -- "$menu_xml"
