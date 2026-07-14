#!/bin/bash
# Quick-access wmenu popup for the waybar SYN-SHARE indicator's on-click.
# Deliberately a second, smaller action list, not shared with or generated
# from syn-share-menu.zsh (the full labwc submenu) — accepted duplication
# for one-click bar access. See syn-share-menu.zsh for the complete set.
set -euo pipefail

SHARE="/usr/lib/syn-os/syn-share.zsh"

CURRENT_THEME_FILE="$HOME/.config/syn-os/current-theme"
THEMES_DIR="$HOME/.config/syn-os/themes"
theme_name="SYN-OS-RED"
[ -f "$CURRENT_THEME_FILE" ] && theme_name="$(<"$CURRENT_THEME_FILE")"
theme_file="$THEMES_DIR/$theme_name.theme"
[ -f "$theme_file" ] && source "$theme_file"
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
