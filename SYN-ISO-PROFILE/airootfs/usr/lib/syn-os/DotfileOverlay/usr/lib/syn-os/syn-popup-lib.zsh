#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P O P U P - L I B
#
#   One shared "run this, show me the result" popup for every menu.xml
#   tool — foot windowed as an undecorated, centered card (via rc.xml's
#   syn-os-popup windowRule) that closes itself on success and pauses
#   with the exit code on failure, instead of the bare terminal window
#   every tool used to open (and never closed) on its own.
#
#   Usage: source this, then: syn_popup::run <argv...>
#   Replaces the process (exec) — call it last.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-POPUP-LIB (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

syn_popup::run() {
  exec foot --app-id=syn-os-popup \
    -o main.pad="24x20 center" \
    -e zsh -c '
    "$@"
    rc=$?
    if (( rc != 0 )); then
      print
      print "[exit $rc — press any key to close]"
      read -k1 -s
    fi
    exit $rc
  ' -- "$@"
}
