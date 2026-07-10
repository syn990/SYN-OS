#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - S H A R E - L I B
#
#   Every action function for the SYN-SHARE file-transfer hub (rsync/Samba
#   /NFS/HTTP/TFTP/Netcat, server + client sides). Sourced only, never
#   executed directly — see syn-share.zsh for the dispatcher that calls
#   these by name.
#
#   Dependencies : nfs-utils rsync samba busybox tftp-hpa openbsd-netcat
#                  pv cifs-utils wget curl openssh
#   Auth model   : rsyncd secrets file, Samba smb password, SSH tunnel
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-SHARE (File Transfer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true
autoload -Uz colors; colors
source /usr/lib/syn-os/syn-ui.zsh

# ── Globals ─────────────────────────────────────────────────────────────────
typeset -g NFS_DIR="/srv/syn-share/nfs"
typeset -g RSYNC_DIR="/srv/syn-share/rsync"
typeset -g SMB_DIR="/srv/syn-share/samba"
typeset -g HTTP_DIR="/srv/syn-share/http"
typeset -g TFTP_DIR="/srv/syn-share/tftp"
typeset -g NC_DEST="/srv/syn-share/incoming"
typeset -g HTTP_PORT="8080"
typeset -g NC_PORT="7000"
typeset -g RSYNC_PORT="8730"          # non-standard → less noise
typeset -g SUBNET="192.168.0.0/16"    # wide enough for most OTG scenarios - add your own here!
typeset -g SECRETS_FILE="/etc/syn-share/rsync.secrets"
typeset -g CONF_DIR="/etc/syn-share"
typeset -g LOG_FILE="/var/log/syn-share.log"
typeset -g LOCAL_PULL="$HOME/syn-share-pull"
typeset -g MNT_NFS="/mnt/syn-share-nfs"
typeset -g MNT_SMB="/mnt/syn-share-smb"
typeset -g RSYNC_USER="synshare"
typeset -g RSYNC_PASS=""
typeset -g SMB_USER="synshare"
typeset -g SMB_PASS=""
typeset -g C_SERVER_IP=""
typeset -g C_RSYNC_PASS=""
typeset -g C_SMB_PASS=""

# ── Logging ─────────────────────────────────────────────────────────────────
log() { print -r -- "[$(date '+%H:%M:%S')] $*" >> "$LOG_FILE" 2>/dev/null || true; }

# ── Plain status messages (no cursor positioning — just terminal output) ────
# Thin wrappers over syn_ui:: so SYN-SHARE's output matches the rest of the
# installer/desktop tools' red/gold palette instead of its own separate
# green/red %F{} scheme — same ✓/✗ call-site names, real shared styling
# underneath.
status_ok()  { syn_ui::step_done "$*"; }
status_err() { syn_ui::error "$*"; }

# ── y/N confirmation ──────────────────────────────────────────────────────
confirm() {
  local msg="$1" reply
  read -q "reply?${msg} [y/N] "
  print
  [[ "$reply" == 'y' || "$reply" == 'Y' ]]
}

# ── Service probe ─────────────────────────────────────────────────────────
tcp_open() { nc -z -w1 "$1" "$2" >/dev/null 2>&1; }
udp_open()  { nc -uz -w1 "$1" "$2" >/dev/null 2>&1; }

typeset -gi P_NFS=0 P_RSYNC=0 P_SMB=0 P_HTTP=0 P_TFTP=0 P_NC=0 P_SSH=0

probe_host() {
  local ip="$1"
  P_NFS=0; P_RSYNC=0; P_SMB=0; P_HTTP=0; P_TFTP=0; P_NC=0; P_SSH=0
  tcp_open "$ip" 2049           && P_NFS=1
  tcp_open "$ip" "$RSYNC_PORT"  && P_RSYNC=1
  tcp_open "$ip" 445            && P_SMB=1
  tcp_open "$ip" "$HTTP_PORT"   && P_HTTP=1
  udp_open "$ip" 69             && P_TFTP=1
  tcp_open "$ip" "$NC_PORT"     && P_NC=1
  tcp_open "$ip" 22             && P_SSH=1
}

# ── Package helpers ───────────────────────────────────────────────────────
is_root() { [[ $EUID -eq 0 ]]; }

pkg_ok() { pacman -Qi "$1" >/dev/null 2>&1; }

ensure_pkg() {
  local p="$1"
  if ! pkg_ok "$p"; then
    status_ok "Installing $p …"
    doas mkdir -p "${LOG_FILE:h}"; doas touch "$LOG_FILE"; doas chmod 666 "$LOG_FILE"
    doas pacman -Sy --noconfirm --needed "$p" >> "$LOG_FILE" 2>&1 || {
      status_err "Failed to install $p"; return 1; }
  fi
}

# ── Firewall helper ───────────────────────────────────────────────────────
fw_tcp() { command -v nft >/dev/null 2>&1 && nft add rule inet filter input tcp dport "$1" accept 2>/dev/null || true; }
fw_udp() { command -v nft >/dev/null 2>&1 && nft add rule inet filter input udp dport "$1" accept 2>/dev/null || true; }

# ── Auth setup ─────────────────────────────────────────────────────────────
setup_rsync_auth() {
  doas mkdir -p "$CONF_DIR"
  printf '%s:%s\n' "$RSYNC_USER" "$RSYNC_PASS" | doas tee "$SECRETS_FILE" >/dev/null
  doas chmod 600 "$SECRETS_FILE"
  log "rsync secrets written"
}

setup_smb_auth() {
  if ! id "$SMB_USER" >/dev/null 2>&1; then
    doas useradd -r -s /usr/bin/nologin "$SMB_USER" 2>/dev/null || true
  fi
  printf '%s\n%s\n' "$SMB_PASS" "$SMB_PASS" | doas smbpasswd -s -a "$SMB_USER" 2>/dev/null || true
  log "Samba password set for $SMB_USER"
}

svc_active() { systemctl is-active --quiet "$1" 2>/dev/null; }

# ── Service status (plain text) ───────────────────────────────────────────
status_all() {
  syn_ui::step "SYN-SHARE service status"
  svc_active rsyncd          && echo "rsyncd: up" || echo "rsyncd: down"
  svc_active smb             && echo "Samba:  up" || echo "Samba:  down"
  svc_active nfs-server      && echo "NFS:    up" || echo "NFS:    down"
  svc_active synshare-httpd  && echo "HTTP:   up" || echo "HTTP:   down"
  svc_active synshare-tftpd  && echo "TFTP:   up" || echo "TFTP:   down"
  svc_active synshare-nc     && echo "Netcat: up" || echo "Netcat: down"
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#   SERVER ACTIONS
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

srv_setup_dirs() {
  # Called first, unconditionally, by every srv_start_* — ensure_pkg's own
  # doas only fires when the package isn't already installed, which isn't
  # reliably true (rsync/samba/etc. are often present from a prior run),
  # so this is the one doas call that's actually always the first prompt
  # of a server-start action.
  local d first=1
  for d in "$NFS_DIR" "$RSYNC_DIR" "$SMB_DIR" "$HTTP_DIR" "$TFTP_DIR" "$NC_DEST" "$CONF_DIR"; do
    if (( first )); then
      syn_ui::doas mkdir -p "$d"
      first=0
    else
      doas mkdir -p "$d"
    fi
  done
  doas touch "$LOG_FILE"
  doas chmod 777 "$NC_DEST" "$RSYNC_DIR"
  # doas elevates the command, not this shell's own >>"$LOG_FILE" redirects
  # below — the file needs to stay writable by the calling user too.
  doas chmod 666 "$LOG_FILE"
}

# ── rsync daemon ───────────────────────────────────────────────────────────
srv_start_rsync() {
  local pass="${1:-}"
  ensure_pkg rsync || return
  srv_setup_dirs

  [[ -n "$pass" ]] && RSYNC_PASS="$pass"
  [[ -z "$RSYNC_PASS" ]] && { status_err "Password required for rsync"; return 1; }
  setup_rsync_auth

  doas tee /etc/rsyncd.conf >/dev/null <<EOF
uid = nobody
gid = nobody
use chroot = no
max connections = 16
port = ${RSYNC_PORT}
log file = ${LOG_FILE}
pid file = /run/rsyncd.pid
hosts allow = ${SUBNET}
hosts deny = *
auth users = ${RSYNC_USER}
secrets file = ${SECRETS_FILE}
[share]
    path = ${RSYNC_DIR}
    comment = SYN-SHARE
    read only = false
    list = yes
    auth users = ${RSYNC_USER}
    secrets file = ${SECRETS_FILE}
EOF

  # rsync's package already ships a real rsyncd.service — no transient unit
  # needed here, and systemd would refuse one under the same name anyway.
  if doas systemctl enable --now rsyncd >>"$LOG_FILE" 2>&1; then
    fw_tcp "$RSYNC_PORT"
    status_ok "rsyncd started on :${RSYNC_PORT}  user=${RSYNC_USER}"
    log "rsyncd started"
  else
    status_err "rsyncd failed to start — see ${LOG_FILE}"
    log "rsyncd failed to start"
    return 1
  fi
}

srv_stop_rsync() { syn_ui::doas systemctl disable --now rsyncd >>"$LOG_FILE" 2>&1 || true; status_ok "rsyncd stopped"; }

# ── Samba ──────────────────────────────────────────────────────────────────
srv_start_samba() {
  local pass="${1:-}"
  ensure_pkg samba || return
  srv_setup_dirs

  [[ -n "$pass" ]] && SMB_PASS="$pass"
  [[ -z "$SMB_PASS" ]] && { status_err "Password required for Samba"; return 1; }
  setup_smb_auth

  doas tee /etc/samba/smb.conf >/dev/null <<EOF
[global]
   server role = standalone server
   workgroup = SYNSHARE
   map to guest = Never
   smb2 leases = yes
[share]
   path = ${SMB_DIR}
   browsable = yes
   writable = yes
   guest ok = no
   valid users = ${SMB_USER}
   force user = nobody
   create mask = 0664
   directory mask = 0775
EOF

  if doas systemctl enable --now smb nmb >>"$LOG_FILE" 2>&1; then
    fw_tcp 139; fw_tcp 445; fw_udp 137; fw_udp 138
    status_ok "Samba started  user=${SMB_USER}  share=\\\\$(hostname -s)\\share"
    log "Samba started"
  else
    status_err "Samba failed to start — see ${LOG_FILE}"
    log "Samba failed to start"
    return 1
  fi
}

srv_stop_samba() { syn_ui::doas systemctl disable --now smb nmb >>"$LOG_FILE" 2>&1 || true; status_ok "Samba stopped"; }

# ── NFS ────────────────────────────────────────────────────────────────────
srv_start_nfs() {
  ensure_pkg nfs-utils || return
  srv_setup_dirs
  doas chown nobody:nobody "$NFS_DIR"; doas chmod 0775 "$NFS_DIR"
  grep -qF "$NFS_DIR" /etc/exports 2>/dev/null || \
    printf '%s  %s(rw,sync,no_subtree_check,fsid=0,no_root_squash)\n' \
      "$NFS_DIR" "$SUBNET" | doas tee -a /etc/exports >/dev/null
  if doas systemctl enable --now nfs-server >>"$LOG_FILE" 2>&1; then
    doas exportfs -rav >>"$LOG_FILE" 2>&1
    fw_tcp 2049
    status_ok "NFSv4 started  →  mount -t nfs4 $(hostname -s):/ /mnt/..."
    log "NFSv4 started"
  else
    status_err "NFSv4 failed to start — see ${LOG_FILE}"
    log "NFSv4 failed to start"
    return 1
  fi
}

srv_stop_nfs() {
  syn_ui::doas systemctl disable --now nfs-server >>"$LOG_FILE" 2>&1 || true
  status_ok "NFSv4 stopped"
}

# ── HTTP (read-only) ───────────────────────────────────────────────────────
srv_start_http() {
  ensure_pkg busybox || return
  srv_setup_dirs
  if doas systemd-run --unit=synshare-httpd \
    /usr/bin/busybox httpd -f -p "0.0.0.0:${HTTP_PORT}" \
    -h "$HTTP_DIR" >>"$LOG_FILE" 2>&1; then
    fw_tcp "$HTTP_PORT"
    status_ok "HTTP on :${HTTP_PORT}  root=${HTTP_DIR}  (read-only)"
    log "HTTP started"
  else
    status_err "HTTP failed to start — see ${LOG_FILE}"
    log "HTTP failed to start"
    return 1
  fi
}

srv_stop_http() { syn_ui::doas systemctl stop synshare-httpd 2>/dev/null || true; status_ok "HTTP stopped"; }

# ── TFTP ───────────────────────────────────────────────────────────────────
srv_start_tftp() {
  ensure_pkg tftp-hpa || return
  srv_setup_dirs
  doas chown -R nobody:nobody "$TFTP_DIR"; doas chmod -R 0775 "$TFTP_DIR"
  doas tee /etc/systemd/system/synshare-tftpd.service >/dev/null <<EOF
[Unit]
Description=SYN-SHARE TFTP
After=network.target
[Service]
ExecStart=/usr/bin/in.tftpd --foreground --address 0.0.0.0:69 --secure ${TFTP_DIR}
Restart=on-failure
[Install]
WantedBy=multi-user.target
EOF
  doas systemctl daemon-reload
  if doas systemctl enable --now synshare-tftpd >>"$LOG_FILE" 2>&1; then
    fw_udp 69
    status_ok "TFTP started  root=${TFTP_DIR}"
    log "TFTP started"
  else
    status_err "TFTP failed to start — see ${LOG_FILE}"
    log "TFTP failed to start"
    return 1
  fi
}

srv_stop_tftp() { syn_ui::doas systemctl disable --now synshare-tftpd 2>/dev/null || true; status_ok "TFTP stopped"; }

# ── Netcat receiver ────────────────────────────────────────────────────────
srv_start_nc() {
  ensure_pkg openbsd-netcat || true
  ensure_pkg pv || true
  srv_setup_dirs
  if doas systemd-run --unit=synshare-nc \
    bash -lc "cd '${NC_DEST}' && nc -lvkp ${NC_PORT} | pv | tar xpf -" >>"$LOG_FILE" 2>&1; then
    fw_tcp "$NC_PORT"
    status_ok "Netcat receiver on :${NC_PORT}  dest=${NC_DEST}"
    log "Netcat receiver started"
  else
    status_err "Netcat receiver failed to start — see ${LOG_FILE}"
    log "Netcat receiver failed to start"
    return 1
  fi
}

srv_stop_nc() { syn_ui::doas systemctl stop synshare-nc 2>/dev/null || true; status_ok "Netcat stopped"; }

# ── OTG quick-start ────────────────────────────────────────────────────────
srv_otg_start() {
  local pass="${1:-}"
  status_ok "OTG: starting rsync + HTTP …"
  srv_start_rsync "$pass"
  srv_start_http
  status_ok "OTG ready  •  rsync://${RSYNC_USER}@$(hostname -I | awk '{print $1}'):${RSYNC_PORT}/share  •  http://$(hostname -I | awk '{print $1}'):${HTTP_PORT}"
  log "OTG mode started"
}

# ── Stop all ───────────────────────────────────────────────────────────────
srv_stop_all() {
  srv_stop_rsync; srv_stop_samba; srv_stop_nfs
  srv_stop_http; srv_stop_tftp; srv_stop_nc
  status_ok "All services stopped"
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#   CLIENT ACTIONS
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

cli_set_server() {
  local ip="${1:-}"
  [[ -z "$ip" ]] && { status_err "No address given"; return 1; }
  C_SERVER_IP="$ip"
  status_ok "Probing ${C_SERVER_IP} …"
  probe_host "$C_SERVER_IP"
  local found=""
  (( P_RSYNC )) && found+="rsync "
  (( P_SMB   )) && found+="SMB "
  (( P_NFS   )) && found+="NFS "
  (( P_HTTP  )) && found+="HTTP "
  (( P_TFTP  )) && found+="TFTP "
  (( P_NC    )) && found+="Netcat "
  (( P_SSH   )) && found+="SSH "
  [[ -z "$found" ]] && found="(none detected)"
  status_ok "  ${C_SERVER_IP} → ${found}"
  log "Probed $C_SERVER_IP : $found"
  mkdir -p "${HOME}/.config/syn-os"
  print -r -- "$C_SERVER_IP" > "${HOME}/.config/syn-os/syn-share-server"
}

cli_rsync_pull() {
  local ip="${1:-}" pass="${2:-}"
  [[ -z "$ip" ]] && { status_err "Server IP required"; return 1; }
  C_SERVER_IP="$ip"
  [[ -n "$pass" ]] && C_RSYNC_PASS="$pass"
  [[ -z "$C_RSYNC_PASS" ]] && { status_err "rsync password required"; return 1; }
  mkdir -p "$LOCAL_PULL"
  RSYNC_PASSWORD="$C_RSYNC_PASS" \
    rsync -avh --progress --port="$RSYNC_PORT" \
    "rsync://${RSYNC_USER}@${C_SERVER_IP}/share/" "${LOCAL_PULL}/" ; local rc=$?
  (( rc == 0 )) && status_ok "Pull complete → ${LOCAL_PULL}" || status_err "rsync pull failed (rc=$rc)"
  log "rsync pull from $C_SERVER_IP rc=$rc"
}

cli_rsync_push() {
  local ip="${1:-}" pass="${2:-}" src="${3:-}"
  [[ -z "$ip" ]] && { status_err "Server IP required"; return 1; }
  C_SERVER_IP="$ip"
  [[ -z "$src" ]] && { status_err "Local path required"; return 1; }
  [[ -n "$pass" ]] && C_RSYNC_PASS="$pass"
  [[ -z "$C_RSYNC_PASS" ]] && { status_err "rsync password required"; return 1; }
  RSYNC_PASSWORD="$C_RSYNC_PASS" \
    rsync -avh --progress --port="$RSYNC_PORT" \
    "$src" "rsync://${RSYNC_USER}@${C_SERVER_IP}/share/" ; local rc=$?
  (( rc == 0 )) && status_ok "Push complete" || status_err "rsync push failed (rc=$rc)"
  log "rsync push to $C_SERVER_IP rc=$rc"
}

cli_smb_mount() {
  local ip="${1:-}" pass="${2:-}"
  [[ -z "$ip" ]] && { status_err "Server IP required"; return 1; }
  C_SERVER_IP="$ip"
  [[ -n "$pass" ]] && C_SMB_PASS="$pass"
  [[ -z "$C_SMB_PASS" ]] && { status_err "Samba password required"; return 1; }
  syn_ui::doas mkdir -p "$MNT_SMB"
  doas mount -t cifs "//${C_SERVER_IP}/share" "$MNT_SMB" \
    -o "user=${SMB_USER},password=${C_SMB_PASS},vers=3.0" >>"$LOG_FILE" 2>&1 && \
    status_ok "SMB mounted → ${MNT_SMB}" || status_err "SMB mount failed (check log)"
}

cli_smb_umount() { syn_ui::doas umount "$MNT_SMB" 2>/dev/null && status_ok "SMB unmounted" || status_err "Not mounted?"; }

cli_nfs_mount() {
  local ip="${1:-}" use_tunnel="${2:-0}"
  [[ -z "$ip" ]] && { status_err "Server IP required"; return 1; }
  C_SERVER_IP="$ip"
  if [[ "$use_tunnel" == "1" ]]; then
    local tunnel_port="20490"
    ssh -fNL "${tunnel_port}:localhost:2049" "$C_SERVER_IP" >>"$LOG_FILE" 2>&1
    syn_ui::doas mkdir -p "$MNT_NFS"
    doas mount -t nfs4 "localhost:/" "$MNT_NFS" -o "port=${tunnel_port}" >>"$LOG_FILE" 2>&1 && \
      status_ok "NFS mounted via SSH tunnel → ${MNT_NFS}" || status_err "NFS mount failed"
  else
    syn_ui::doas mkdir -p "$MNT_NFS"
    doas mount -t nfs4 "${C_SERVER_IP}:/" "$MNT_NFS" >>"$LOG_FILE" 2>&1 && \
      status_ok "NFS mounted → ${MNT_NFS}" || status_err "NFS mount failed"
  fi
}

cli_nfs_umount() { syn_ui::doas umount "$MNT_NFS" 2>/dev/null && status_ok "NFS unmounted" || status_err "Not mounted?"; }

cli_http_mirror() {
  local ip="${1:-}"
  [[ -z "$ip" ]] && { status_err "Server IP required"; return 1; }
  C_SERVER_IP="$ip"
  mkdir -p "$LOCAL_PULL"
  wget -m -np -nH --cut-dirs=0 "http://${C_SERVER_IP}:${HTTP_PORT}/" -P "$LOCAL_PULL" ; local rc=$?
  (( rc == 0 )) && status_ok "HTTP mirror → ${LOCAL_PULL}" || status_err "wget failed (rc=$rc)"
}

cli_tftp_get() {
  local ip="${1:-}" f="${2:-}"
  [[ -z "$ip" ]] && { status_err "Server IP required"; return 1; }
  C_SERVER_IP="$ip"
  [[ -z "$f" ]] && { status_err "Remote filename required"; return 1; }
  tftp "$C_SERVER_IP" -m binary -c get "$f"; local rc=$?
  (( rc == 0 )) && status_ok "TFTP GET → ./${f}" || status_err "TFTP GET failed"
}

cli_tftp_put() {
  local ip="${1:-}" f="${2:-}"
  [[ -z "$ip" ]] && { status_err "Server IP required"; return 1; }
  C_SERVER_IP="$ip"
  [[ -z "$f" || ! -f "$f" ]] && { status_err "File not found: $f"; return 1; }
  tftp "$C_SERVER_IP" -m binary -c put "$f"; local rc=$?
  (( rc == 0 )) && status_ok "TFTP PUT done" || status_err "TFTP PUT failed"
}

cli_nc_send() {
  local ip="${1:-}" src="${2:-}"
  [[ -z "$ip" ]] && { status_err "Server IP required"; return 1; }
  C_SERVER_IP="$ip"
  [[ -z "$src" || ! -e "$src" ]] && { status_err "Path not found: $src"; return 1; }
  local dir="${src:h}" base="${src:t}"
  ( cd "$dir" && tar cpf - "$base" ) | pv | nc -q 0 "$C_SERVER_IP" "$NC_PORT"; local rc=$?
  (( rc == 0 )) && status_ok "Netcat send done" || status_err "Netcat send failed (rc=$rc)"
}

cli_ssh_copy() {
  local ip="${1:-}" src="${2:-}" dst="${3:-}"
  [[ -z "$ip" ]] && { status_err "Server IP required"; return 1; }
  C_SERVER_IP="$ip"
  [[ -z "$src" ]] && { status_err "Local path required"; return 1; }
  [[ -z "$dst" ]] && dst="${C_SERVER_IP}:~/syn-share-recv/"
  scp -r "$src" "$dst"; local rc=$?
  (( rc == 0 )) && status_ok "SCP done" || status_err "SCP failed"
}
