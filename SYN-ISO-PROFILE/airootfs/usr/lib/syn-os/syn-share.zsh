#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                           S Y N - S H A R E
#
#   File-transfer hub for SYN-OS (rsync/Samba/NFS/HTTP/TFTP/Netcat, server +
#   client). Thin dispatcher over syn-share-lib.zsh's action functions —
#   reached via labwc's SYN-SHARE pipe-menu (syn-pipe-share.zsh),
#   not meant to be run interactively.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-SHARE (File Transfer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

source /usr/lib/syn-os/syn-share-lib.zsh

usage() {
  cat <<'EOF'
Usage: syn-share.zsh <subcommand> [args...]

Server:
  srv-start-rsync <pass>            srv-stop-rsync
  srv-start-samba <pass>            srv-stop-samba
  srv-start-nfs                     srv-stop-nfs
  srv-start-http                    srv-stop-http
  srv-start-tftp                    srv-stop-tftp
  srv-start-nc                      srv-stop-nc
  srv-otg-start <rsync-pass>
  srv-stop-all

Client:
  cli-set-server <ip>
  cli-rsync-pull <ip> <pass>
  cli-rsync-push <ip> <pass> <localpath>
  cli-smb-mount <ip> <pass>          cli-smb-umount
  cli-nfs-mount <ip> <0|1-ssh-tunnel> cli-nfs-umount
  cli-http-mirror <ip>
  cli-tftp-get <ip> <file>           cli-tftp-put <ip> <file>
  cli-nc-send <ip> <path>
  cli-ssh-copy <ip> <path> <dest>

  status                             (plain-text service status)
EOF
}

cmd="${1:-}"
[[ -n "$cmd" ]] && shift

case "$cmd" in
  srv-start-rsync) srv_start_rsync "$@" ;;
  srv-stop-rsync)  srv_stop_rsync ;;
  srv-start-samba) srv_start_samba "$@" ;;
  srv-stop-samba)  srv_stop_samba ;;
  srv-start-nfs)   srv_start_nfs ;;
  srv-stop-nfs)    srv_stop_nfs ;;
  srv-start-http)  srv_start_http ;;
  srv-stop-http)   srv_stop_http ;;
  srv-start-tftp)  srv_start_tftp ;;
  srv-stop-tftp)   srv_stop_tftp ;;
  srv-start-nc)    srv_start_nc ;;
  srv-stop-nc)     srv_stop_nc ;;
  srv-otg-start)   srv_otg_start "$@" ;;
  srv-stop-all)    srv_stop_all ;;

  cli-set-server)  cli_set_server "$@" ;;
  cli-rsync-pull)  cli_rsync_pull "$@" ;;
  cli-rsync-push)  cli_rsync_push "$@" ;;
  cli-smb-mount)   cli_smb_mount "$@" ;;
  cli-smb-umount)  cli_smb_umount ;;
  cli-nfs-mount)   cli_nfs_mount "$@" ;;
  cli-nfs-umount)  cli_nfs_umount ;;
  cli-http-mirror) cli_http_mirror "$@" ;;
  cli-tftp-get)    cli_tftp_get "$@" ;;
  cli-tftp-put)    cli_tftp_put "$@" ;;
  cli-nc-send)     cli_nc_send "$@" ;;
  cli-ssh-copy)    cli_ssh_copy "$@" ;;

  status) status_all ;;

  ""|-h|--help) usage ;;
  *) print -u2 "Unknown subcommand: $cmd"; usage; exit 1 ;;
esac
