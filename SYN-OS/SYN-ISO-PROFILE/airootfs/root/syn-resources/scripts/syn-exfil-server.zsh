#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                  S Y N  –  E X F I L   (Server)
#
#   Provides a unified control panel for enabling and managing all SYN‑OS
#   file‑transfer services (NFSv4, rsyncd, Samba, HTTP, TFTP, and Netcat).
#   Designed for rapid deployment during migrations, diagnostics, or LAN-based
#   replication tasks. Safe to run on bare-metal or live environments.
#
#   SYN‑OS        : The Syntax Operating System
#   Component     : SYN‑EXFIL (Server Control)
#   Author        : William Hayward-Holland (Syntax990)
#   License       : MIT License
# ------------------------------------------------------------------------------

emulate -L zsh
set -e
set -u
# pipefail for zsh
(set -o | grep -q pipefail 2>/dev/null && set -o pipefail) || setopt pipefail 2>/dev/null || true

# ====== Config (adjust if needed) ============================================
typeset -g NFS_DIR="/srv/share"
typeset -g RSYNC_DIR="/srv/rsync/share"
typeset -g SMB_DIR="/srv/samba/fastshare"
typeset -g HTTP_DIR="/srv/httpshare"
typeset -g TFTP_DIR="/srv/tftp"
typeset -g NC_DEST="/srv/incoming"
typeset -g HTTP_PORT="8080"
typeset -g NC_PORT="7000"
typeset -g SUBNET_CIDR="192.168.1.0/24"
# ============================================================================

autoload -Uz colors; colors

print_banner() {
  local B=$fg_bold[blue] G=$fg_bold[green] Y=$fg_bold[yellow] R=$fg_bold[red] N=$reset_color
  print -P "${G}S Y N – E X F I L   ${Y}(Server)   ${N}|  ${G}SYN‑OS / Arch  •  zsh${N}"
  print -P ""
}

need_root() {
  if [[ $EUID -ne 0 ]]; then
    print -P "%F{red}-> Root required. Re-run: sudo $0%f"
    exit 1
  fi
}

install_pkg() {
  local pkg="$1"
  if ! pacman -Qi "$pkg" >/dev/null 2>&1; then
    print -P "%F{yellow}-> Installing $pkg%f"
    pacman -Sy --noconfirm --needed "$pkg"
  fi
}

add_fw_tcp() {
  local port="$1"
  if command -v nft >/dev/null 2>&1; then
    nft list tables | grep -q '^table inet filter$' || nft add table inet filter
    nft list chain inet filter input >/dev/null 2>&1 || nft add chain inet filter input '{ type filter hook input priority 0; policy accept; }'
    nft add rule inet filter input tcp dport "$port" accept 2>/dev/null || true
  fi
}

add_fw_udp() {
  local port="$1"
  if command -v nft >/dev/null 2>&1; then
    nft list tables | grep -q '^table inet filter$' || nft add table inet filter
    nft list chain inet filter input >/dev/null 2>&1 || nft add chain inet filter input '{ type filter hook input priority 0; policy accept; }'
    nft add rule inet filter input udp dport "$port" accept 2>/dev/null || true
  fi
}

show_ips() {
  print -P "%F{cyan}Server IPs:%f"
  ip -4 addr show | awk '/state UP/{i=1} i&&/inet /{printf(" - %s on %s\n", $2, $NF)}'
}

start_nfs() {
  print -P "%F{green}==>%f NFSv4"
  install_pkg nfs-utils
  mkdir -p "$NFS_DIR"
  chown nobody:nobody "$NFS_DIR"
  chmod 0775 "$NFS_DIR"
  if ! grep -q -F "$NFS_DIR" /etc/exports 2>/dev/null; then
    print -r -- "$NFS_DIR  ${SUBNET_CIDR}(rw,sync,no_subtree_check,fsid=0,no_root_squash)" >> /etc/exports
  fi
  systemctl enable --now nfs-server
  exportfs -rav
  add_fw_tcp 2049
  show_ips
  print -P "Mount example: %F{yellow}mount -t nfs4 <server>:/ /mnt/syn_exfil_nfs%f"
}

stop_nfs() { systemctl disable --now nfs-server || true }

start_rsyncd() {
  print -P "%F{green}==>%f rsync daemon"
  install_pkg rsync
  mkdir -p "$RSYNC_DIR"
  chown nobody:nobody "$RSYNC_DIR"
  chmod 0775 "$RSYNC_DIR"

  cat >/etc/rsyncd.conf <<EOF
uid = nobody
gid = nobody
use chroot = no
max connections = 16
log file = /var/log/rsyncd.log
pid file = /run/rsyncd.pid
hosts allow = ${SUBNET_CIDR}
hosts deny = *
[share]
    path = ${RSYNC_DIR}
    comment = SYN-EXFIL rsync portal
    read only = false
    list = yes
EOF

  cat >/etc/systemd/system/rsyncd.service <<'EOF'
[Unit]
Description=rsync daemon
After=network.target

[Service]
ExecStart=/usr/bin/rsync --daemon --no-detach
Restart=on-failure
EOF

  systemctl daemon-reload
  systemctl enable --now rsyncd
  add_fw_tcp 873
  show_ips
  print -P "Browse: %F{yellow}rsync rsync://<server>/%f   |   Module: %F{yellow}share%f"
}

stop_rsyncd() { systemctl disable --now rsyncd || true }

start_samba() {
  print -P "%F{green}==>%f Samba (guest)"
  install_pkg samba
  mkdir -p "$SMB_DIR"
  chown nobody:nobody "$SMB_DIR"
  chmod 0775 "$SMB_DIR"

  cat >/etc/samba/smb.conf <<EOF
[global]
   server role = standalone server
   workgroup = WORKGROUP
   map to guest = Bad User
   smb2 leases = yes
   aio read size = 1
   aio write size = 1

[fastshare]
   path = ${SMB_DIR}
   browsable = yes
   writable = yes
   guest ok = yes
   force user = nobody
   create mask = 0664
   directory mask = 0775
EOF

  systemctl enable --now smb nmb
  add_fw_udp 137; add_fw_udp 138; add_fw_tcp 139; add_fw_tcp 445
  show_ips
  print -P "Mount example: %F{yellow}mount -t cifs //SERVER/fastshare /mnt/syn_exfil_smb -o guest,vers=3.0%f"
}

stop_samba() { systemctl disable --now smb nmb || true }

start_httpd() {
  print -P "%F{green}==>%f BusyBox httpd (read-only)"
  install_pkg busybox
  mkdir -p "$HTTP_DIR"
  cd "$HTTP_DIR"
  systemd-run --unit=synexfil-httpd /usr/bin/busybox httpd -f -p 0.0.0.0:"$HTTP_PORT" >/dev/null
  add_fw_tcp "$HTTP_PORT"
  show_ips
  print -P "Open: %F{yellow}http://<server>:${HTTP_PORT}/%f"
}

stop_httpd() { systemctl stop synexfil-httpd || true }

start_tftp() {
  print -P "%F{green}==>%f TFTP (tftp-hpa)"
  install_pkg tftp-hpa
  mkdir -p "$TFTP_DIR"
  chown -R nobody:nobody "$TFTP_DIR"
  chmod -R 0775 "$TFTP_DIR"

  cat >/etc/systemd/system/synexfil-tftpd.service <<EOF
[Unit]
Description=Simple TFTP server (SYN-EXFIL)
After=network.target

[Service]
ExecStart=/usr/bin/in.tftpd --listen --address 0.0.0.0:69 --secure ${TFTP_DIR}
Restart=on-failure

[Install]
WantedBy=multi-user.target
EOF

  systemctl daemon-reload
  systemctl enable --now synexfil-tftpd
  add_fw_udp 69
  show_ips
  print -P "Usage: %F{yellow}tftp <server> -m binary -c get/put <file>%f"
}

stop_tftp() { systemctl disable --now synexfil-tftpd || true }

start_nc_receiver() {
  print -P "%F{green}==>%f Netcat receiver (tar) : ${NC_PORT}/tcp"
  install_pkg pv || true
  command -v nc >/dev/null 2>&1 || install_pkg openbsd-netcat
  mkdir -p "$NC_DEST"
  systemd-run --unit=synexfil-nc bash -lc "cd '${NC_DEST}' && nc -lvkp ${NC_PORT} | pv | tar xpf -" >/dev/null
  add_fw_tcp "$NC_PORT"
  show_ips
  print -P "Send example: %F{yellow}tar cpf - <src> | pv | nc -q 0 <server> ${NC_PORT}%f"
}

stop_nc_receiver() { systemctl stop synexfil-nc || true }

status_all() {
  print -P "%F{cyan}---- STATUS ----%f"
  systemctl is-active --quiet nfs-server      && echo "NFS: up"     || echo "NFS: down"
  systemctl is-active --quiet rsyncd          && echo "rsyncd: up"  || echo "rsyncd: down"
  systemctl is-active --quiet smb             && echo "Samba: up"   || echo "Samba: down"
  systemctl is-active --quiet synexfil-httpd  && echo "HTTP: up"    || echo "HTTP: down"
  systemctl is-active --quiet synexfil-tftpd  && echo "TFTP: up"    || echo "TFTP: down"
  systemctl is-active --quiet synexfil-nc     && echo "Netcat: up"  || echo "Netcat: down"
  print -P "%F{cyan}----------------%f"
}

stop_all() {
  stop_nc_receiver; stop_httpd; stop_tftp; stop_samba; stop_rsyncd; stop_nfs
  print -P "%F{yellow}All services stopped.%f"
}

menu() {
  print_banner
  while true; do
    print -P "%F{magenta}===== SYN‑EXFIL Server =====%f"
    print "1) Start NFSv4"
    print "2) Start rsync daemon"
    print "3) Start Samba (guest)"
    print "4) Start HTTP (read-only) on ${HTTP_PORT}"
    print "5) Start TFTP"
    print "6) Start Netcat receiver on ${NC_PORT}"
    print "7) Stop ALL"
    print "8) Status"
    print "9) Quit"
    read -r "c?Select: "
    case "$c" in
      1) start_nfs ;;
      2) start_rsyncd ;;
      3) start_samba ;;
      4) start_httpd ;;
      5) start_tftp ;;
      6) start_nc_receiver ;;
      7) stop_all ;;
      8) status_all ;;
      9) exit 0 ;;
      *) print -P "%F{red}Invalid%f" ;;
    esac
    print
  done
}

need_root
menu
