#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                 S Y N - B A R - S H A R E - Q U I C K M E N U
#
#   Quick-access wmenu popup for waybar's SYN-SHARE indicator on-click — a
#   deliberately smaller, separate action list from syn-pipe-share.zsh's
#   full labwc submenu, for one-click bar access.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BAR-SHARE-QUICKMENU (Waybar)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

SHARE="/usr/lib/syn-os/syn-share.zsh"

source /usr/lib/syn-os/syn-theme-lib.zsh
syn_theme_load
SYN_BG="${SYN_BG:-#000000}"
SYN_TEXT="${SYN_TEXT:-#ffffff}"
SYN_PANEL_HOVER="${SYN_PANEL_HOVER:-#400101}"
SYN_ACCENT_DIM="${SYN_ACCENT_DIM:-#260101}"

choice=$(printf '%s\n' \
  "Start rsync" "Stop rsync" \
  "Start Samba" "Stop Samba" \
  "Start NFS" "Stop NFS" \
  "Start HTTP" "Stop HTTP" \
  "Start TFTP" "Stop TFTP" \
  "Start Netcat" "Stop Netcat" \
  "Set / Probe Server" \
  "rsync: Pull" \
  "Stop ALL services" \
  "Service Status" \
  | wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "SYN-SHARE:")

[ -z "$choice" ] && exit 0

case "$choice" in
  "Start rsync")
    pass=$(printf '' | wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "rsync password:")
    exec foot -e zsh -c "zsh $SHARE srv-start-rsync '$pass'; exec zsh"
    ;;
  "Stop rsync")  exec foot -e zsh -c "zsh $SHARE srv-stop-rsync; exec zsh" ;;
  "Start Samba")
    pass=$(printf '' | wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "Samba password:")
    exec foot -e zsh -c "zsh $SHARE srv-start-samba '$pass'; exec zsh"
    ;;
  "Stop Samba")  exec foot -e zsh -c "zsh $SHARE srv-stop-samba; exec zsh" ;;
  "Start NFS")   exec foot -e zsh -c "zsh $SHARE srv-start-nfs; exec zsh" ;;
  "Stop NFS")    exec foot -e zsh -c "zsh $SHARE srv-stop-nfs; exec zsh" ;;
  "Start HTTP")  exec foot -e zsh -c "zsh $SHARE srv-start-http; exec zsh" ;;
  "Stop HTTP")   exec foot -e zsh -c "zsh $SHARE srv-stop-http; exec zsh" ;;
  "Start TFTP")  exec foot -e zsh -c "zsh $SHARE srv-start-tftp; exec zsh" ;;
  "Stop TFTP")   exec foot -e zsh -c "zsh $SHARE srv-stop-tftp; exec zsh" ;;
  "Start Netcat") exec foot -e zsh -c "zsh $SHARE srv-start-nc; exec zsh" ;;
  "Stop Netcat") exec foot -e zsh -c "zsh $SHARE srv-stop-nc; exec zsh" ;;
  "Set / Probe Server")
    ip=$(printf '' | wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "Server IP or hostname:")
    exec foot -e zsh -c "zsh $SHARE cli-set-server '$ip'; exec zsh"
    ;;
  "rsync: Pull")
    ip=$(printf '' | wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "Server IP:")
    pass=$(printf '' | wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "rsync password:")
    exec foot -e zsh -c "zsh $SHARE cli-rsync-pull '$ip' '$pass'; exec zsh"
    ;;
  "Stop ALL services")
    confirm=$(printf '' | wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_PANEL_HOVER" -s "$SYN_TEXT" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "Type yes to stop ALL:")
    [ "$confirm" = "yes" ] && exec foot -e zsh -c "zsh $SHARE srv-stop-all; exec zsh"
    ;;
  "Service Status")
    exec foot -e zsh -c "zsh $SHARE status; echo; read -k1 -s '?Press any key'"
    ;;
esac
