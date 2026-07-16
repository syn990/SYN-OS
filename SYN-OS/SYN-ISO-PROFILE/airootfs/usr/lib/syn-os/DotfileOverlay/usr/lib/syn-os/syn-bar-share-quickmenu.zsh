#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                 S Y N - B A R - S H A R E - Q U I C K M E N U
#
#   Quick-access rofi popup for waybar's SYN-SHARE indicator on-click — a
#   deliberately smaller, separate action list from syn-pipe-share.zsh's
#   full labwc submenu, for one-click bar access. Only ever offers Start
#   for a stopped service or Stop for a running one — never both — same
#   as syn-pipe-share.zsh's own toggle_item, so the two don't drift into
#   showing different things for the same live state.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BAR-SHARE-QUICKMENU (Waybar)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

PROMPT="/usr/lib/syn-os/syn-share-prompt.zsh"

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
syn_theme_load

svc_active() { systemctl is-active --quiet "$1" 2>/dev/null; }

# toggle_line <unit> <label> <start-keyword> <stop-keyword>
# Echoes one menu line ("Start X" or "Stop X") and remembers which
# keyword it maps to, same shape as syn-pipe-share.zsh's toggle_item.
typeset -A LINE_KEYWORD
toggle_line() {
  local unit="$1" label="$2" start_kw="$3" stop_kw="$4"
  local line
  if svc_active "$unit"; then
    line="Stop ${label}"
    LINE_KEYWORD[$line]="$stop_kw"
  else
    line="Start ${label}"
    LINE_KEYWORD[$line]="$start_kw"
  fi
  print -r -- "$line"
}

menu_lines=(
  "$(toggle_line rsyncd         rsync  srv-start-rsync srv-stop-rsync)"
  "$(toggle_line smb            Samba  srv-start-samba srv-stop-samba)"
  "$(toggle_line nfs-server      NFS    srv-start-nfs   srv-stop-nfs)"
  "$(toggle_line synshare-httpd HTTP   srv-start-http  srv-stop-http)"
  "$(toggle_line synshare-tftpd TFTP   srv-start-tftp  srv-stop-tftp)"
  "$(toggle_line synshare-nc    Netcat srv-start-nc    srv-stop-nc)"
)

choice=$(printf '%s\n' \
  "${menu_lines[@]}" \
  "Set / Probe Server" \
  "rsync: Pull" \
  "Stop ALL services" \
  "Service Status" \
  | syn_pick::rofi "SYN-SHARE:")

[ -z "$choice" ] && exit 0

case "$choice" in
  "Set / Probe Server") exec "$PROMPT" cli-set-server ;;
  "rsync: Pull")        exec "$PROMPT" cli-rsync-pull ;;
  "Stop ALL services")  exec "$PROMPT" srv-stop-all ;;
  "Service Status")
    exec foot -e zsh -c 'zsh /usr/lib/syn-os/syn-share.zsh status; echo; read -k1 -s "?Press any key"'
    ;;
  *)
    kw="${LINE_KEYWORD[$choice]:-}"
    if [ -n "$kw" ]; then
      exec "$PROMPT" "$kw"
    fi
    ;;
esac
