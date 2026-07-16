#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                       S Y N - S H A R E - P R O M P T
#
#   Collects the rofi popups a SYN-SHARE menu item needs (password, IP,
#   path...), then execs foot to run the real syn-share.zsh subcommand.
#   Takes one bare keyword, not a command string to reparse — labwc's
#   own <command> tokenization isn't shell quoting (see labwc-actions(5):
#   it goes straight to execvp(), no shell in between), so passing
#   anything with embedded quotes/spaces through menu.xml is exactly the
#   kind of thing that silently mangles. Every prompt/field-list lives
#   in this script instead, keyed by keyword.
#
#   Usage: syn-share-prompt.zsh <keyword>
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-SHARE-PROMPT (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

SHARE="/usr/lib/syn-os/syn-share.zsh"
SERVER_FILE="$HOME/.config/syn-os/syn-share-server"

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
syn_theme_load

saved_ip=""
[[ -f "$SERVER_FILE" ]] && saved_ip="$(<"$SERVER_FILE")"

keyword="${1:?Usage: syn-share-prompt.zsh <keyword>}"

# --app-id=syn-share-popup matches rc.xml's windowRule: undecorated,
# fixed-size, centered — a real popup, not a terminal you have to close
# yourself. The -o overrides add a themed border foot draws itself (not
# server-side, so serverDecoration=no doesn't remove it) and extra
# padding, so it reads as a deliberate card instead of a bare rectangle.
# Closes itself on success; on failure it waits for a keypress so the
# error is actually readable instead of vanishing instantly.
run() {
  # A [csd] border was tried here, but csd.preferred=client brings back
  # foot's whole titlebar (minimize/maximize/close chrome) to get it —
  # worse than plain undecorated. serverDecoration=no in rc.xml already
  # gives a clean borderless card; just widen the padding a bit more.
  exec foot --app-id=syn-share-popup \
    -o main.pad="24x20 center" \
    -e zsh -c '
    zsh "$1" "${@:2}"
    rc=$?
    if (( rc != 0 )); then
      print
      print "[exit $rc — press any key to close]"
      read -k1 -s
    fi
    exit $rc
  ' -- "$SHARE" "$@"
}

case "$keyword" in
  srv-start-rsync)
    pass="$(syn_pick::rofi_password "rsync password:")"
    run srv-start-rsync "$pass"
    ;;
  srv-start-samba)
    pass="$(syn_pick::rofi_password "Samba password:")"
    run srv-start-samba "$pass"
    ;;
  srv-otg-start)
    pass="$(syn_pick::rofi_password "rsync password:")"
    run srv-otg-start "$pass"
    ;;
  srv-stop-all)
    confirm="$(syn_pick::rofi_input "Type yes to stop ALL:")"
    [[ "$confirm" == yes ]] && run srv-stop-all
    ;;
  # No prompts of their own — still routed through run() so they get the
  # same popup framing as everything else instead of a bare foot window.
  srv-start-nfs|srv-start-http|srv-start-tftp|srv-start-nc)
    run "$keyword"
    ;;
  srv-stop-rsync|srv-stop-samba|srv-stop-nfs|srv-stop-http|srv-stop-tftp|srv-stop-nc)
    run "$keyword"
    ;;
  cli-smb-umount)
    run cli-smb-umount
    ;;
  cli-nfs-umount)
    run cli-nfs-umount
    ;;
  cli-set-server)
    ip="$(syn_pick::rofi_input "Server IP or hostname:" "$saved_ip")"
    run cli-set-server "$ip"
    ;;
  cli-rsync-pull)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    pass="$(syn_pick::rofi_password "rsync password:")"
    run cli-rsync-pull "$ip" "$pass"
    ;;
  cli-rsync-push)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    pass="$(syn_pick::rofi_password "rsync password:")"
    src="$(syn_pick::rofi_input "Local path:")"
    run cli-rsync-push "$ip" "$pass" "$src"
    ;;
  cli-smb-mount)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    pass="$(syn_pick::rofi_password "Samba password:")"
    run cli-smb-mount "$ip" "$pass"
    ;;
  cli-nfs-mount-direct)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    run cli-nfs-mount "$ip" 0
    ;;
  cli-nfs-mount-ssh)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    run cli-nfs-mount "$ip" 1
    ;;
  cli-http-mirror)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    run cli-http-mirror "$ip"
    ;;
  cli-tftp-get)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    f="$(syn_pick::rofi_input "Remote filename:")"
    run cli-tftp-get "$ip" "$f"
    ;;
  cli-tftp-put)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    f="$(syn_pick::rofi_input "Local file:")"
    run cli-tftp-put "$ip" "$f"
    ;;
  cli-nc-send)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    src="$(syn_pick::rofi_input "Local path:")"
    run cli-nc-send "$ip" "$src"
    ;;
  cli-ssh-copy)
    ip="$(syn_pick::rofi_input "Server IP:" "$saved_ip")"
    src="$(syn_pick::rofi_input "Local path:")"
    dst="$(syn_pick::rofi_input "Remote destination:")"
    run cli-ssh-copy "$ip" "$src" "$dst"
    ;;
  *)
    print -u2 "Unknown keyword: $keyword"
    exit 1
    ;;
esac
