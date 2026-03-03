#!/usr/bin/env zsh
# ==============================================================================
#   S Y N – S H A R E
#   Unified file-transfer hub for SYN-OS  •  server + client in one TUI
#
#   Author  : William Hayward-Holland (Syntax990)
#   License : MIT
#
#   Dependencies
#     nfs-utils  rsync  samba  busybox  tftp-hpa
#     openbsd-netcat  pv  cifs-utils  wget  curl  openssh
#
#   Navigation  : arrow keys / hjkl / number shortcuts  •  Enter to select
#   Auth model  : rsyncd secrets file  •  Samba smb password  •  SSH tunnel
# ==============================================================================

emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true
autoload -Uz colors; colors

# ── Globals ────────────────────────────────────────────────────────────────────
typeset -g VERSION="1.3.0"
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
typeset -g RSYNC_PASS=""              # set at runtime
typeset -g SMB_USER="synshare"
typeset -g SMB_PASS=""
typeset -g MODE="menu"                # menu | server | client
typeset -gi SEL=0                     # current menu selection
typeset -g  STATUS_MSG=""
typeset -g  STATUS_OK=1

# ── Terminal primitives ────────────────────────────────────────────────────────
ESC=$'\e'
CSI="${ESC}["

tput_cup()   { printf "${CSI}%d;%dH" "$1" "$2"; }
tput_clear() { printf "${CSI}2J${CSI}H"; }
tput_hide()  { printf "${CSI}?25l"; }
tput_show()  { printf "${CSI}?25h"; }
tput_bold()  { printf "${CSI}1m"; }
tput_dim()   { printf "${CSI}2m"; }
tput_reset() { printf "${CSI}0m"; }

COLS=$(tput cols  2>/dev/null || echo 80)
ROWS=$(tput lines 2>/dev/null || echo 24)

# Palette (256-colour)
C_BG=232        # near-black
C_PANEL=235
C_BORDER=239
C_ACCENT=39     # electric blue
C_GREEN=82
C_RED=196
C_YELLOW=220
C_CYAN=51
C_MAGENTA=135
C_WHITE=255
C_DIM=244

fg256() { printf "${CSI}38;5;%dm" "$1"; }
bg256() { printf "${CSI}48;5;%dm" "$1"; }

set_cell() { fg256 "$1"; bg256 "$2"; }
reset_cell() { tput_reset; }

# ── Box drawing ────────────────────────────────────────────────────────────────
BOX_TL="╭" BOX_TR="╮" BOX_BL="╰" BOX_BR="╯"
BOX_H="─"  BOX_V="│"
BOX_LT="├" BOX_RT="┤" BOX_TT="┬" BOX_BT="┴" BOX_CROSS="┼"

draw_hline() {
  local y=$1 x=$2 w=$3 col=$4
  tput_cup $y $x
  fg256 $col
  printf '%*s' "$w" '' | tr ' ' "$BOX_H"
  reset_cell
}

draw_box() {
  local y=$1 x=$2 h=$3 w=$4 col=${5:-$C_BORDER}
  local inner_w=$(( w - 2 ))
  fg256 $col
  # top
  tput_cup $y $x
  printf '%s%s%s' "$BOX_TL" "$(printf '%*s' $inner_w '' | tr ' ' "$BOX_H")" "$BOX_TR"
  # sides + blank interior rows
  local r
  for (( r=1; r<h-1; r++ )); do
    tput_cup $(( y+r )) $x
    printf '%s' "$BOX_V"
    tput_cup $(( y+r )) $(( x+w-1 ))
    printf '%s' "$BOX_V"
  done
  # bottom
  tput_cup $(( y+h-1 )) $x
  printf '%s%s%s' "$BOX_BL" "$(printf '%*s' $inner_w '' | tr ' ' "$BOX_H")" "$BOX_BR"
  reset_cell
}

# Centre text inside a given width
centre_in() {
  local text="$1" width="$2"
  local len=${#text}
  local pad=$(( (width - len) / 2 ))
  printf '%*s%s%*s' $pad '' "$text" $(( width - len - pad )) ''
}

# ── Logging ────────────────────────────────────────────────────────────────────
log() { print -r -- "[$(date '+%H:%M:%S')] $*" >> "$LOG_FILE" 2>/dev/null || true; }

# ── Status bar ─────────────────────────────────────────────────────────────────
status_ok()  { STATUS_MSG="$*"; STATUS_OK=1;  draw_status; }
status_err() { STATUS_MSG="$*"; STATUS_OK=0;  draw_status; }

draw_status() {
  local row=$(( ROWS - 1 ))
  tput_cup $row 1
  if (( STATUS_OK )); then
    set_cell $C_GREEN $C_BG
  else
    set_cell $C_RED $C_BG
  fi
  printf ' %-*s' $(( COLS - 2 )) "$STATUS_MSG"
  reset_cell
}

# ── Service probe ──────────────────────────────────────────────────────────────
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

# ── Package helpers ────────────────────────────────────────────────────────────
is_root() { [[ $EUID -eq 0 ]]; }

pkg_ok() { pacman -Qi "$1" >/dev/null 2>&1; }

ensure_pkg() {
  local p="$1"
  if ! pkg_ok "$p"; then
    status_ok "Installing $p …"
    sudo pacman -Sy --noconfirm --needed "$p" >> "$LOG_FILE" 2>&1 || {
      status_err "Failed to install $p"; return 1; }
  fi
}

# ── Firewall helper ────────────────────────────────────────────────────────────
fw_tcp() { command -v nft >/dev/null 2>&1 && nft add rule inet filter input tcp dport "$1" accept 2>/dev/null || true; }
fw_udp() { command -v nft >/dev/null 2>&1 && nft add rule inet filter input udp dport "$1" accept 2>/dev/null || true; }

# ── Auth setup ─────────────────────────────────────────────────────────────────
setup_rsync_auth() {
  # Create secrets file; rsync daemon will require user:pass
  sudo mkdir -p "$CONF_DIR"
  printf '%s:%s\n' "$RSYNC_USER" "$RSYNC_PASS" | sudo tee "$SECRETS_FILE" >/dev/null
  sudo chmod 600 "$SECRETS_FILE"
  log "rsync secrets written"
}

setup_smb_auth() {
  # Create Unix user if missing, set Samba password
  if ! id "$SMB_USER" >/dev/null 2>&1; then
    sudo useradd -r -s /usr/bin/nologin "$SMB_USER" 2>/dev/null || true
  fi
  printf '%s\n%s\n' "$SMB_PASS" "$SMB_PASS" | sudo smbpasswd -s -a "$SMB_USER" 2>/dev/null || true
  log "Samba password set for $SMB_USER"
}

# ── Header / ASCII logo ────────────────────────────────────────────────────────
draw_header() {
  local hrow=1
  tput_cup $hrow 1
  set_cell $C_ACCENT $C_BG; tput_bold
  printf ' %-*s' $(( COLS - 2 )) \
    "SYN─SHARE  v${VERSION}  ·  $(date '+%H:%M')  ·  $(hostname -s)"
  reset_cell

  tput_cup $(( hrow+1 )) 1
  fg256 $C_DIM
  printf '%*s' $(( COLS - 2 )) '' | tr ' ' '─'
  reset_cell
}

# ── Sidebar: running service status ───────────────────────────────────────────
SIDE_X=$(( COLS - 24 ))
SIDE_W=24
SIDE_Y=3
SIDE_H=$(( ROWS - 5 ))

svc_active() { systemctl is-active --quiet "$1" 2>/dev/null; }

draw_sidebar() {
  draw_box $SIDE_Y $SIDE_X $SIDE_H $SIDE_W $C_BORDER

  tput_cup $SIDE_Y $(( SIDE_X + 1 ))
  fg256 $C_CYAN; tput_bold
  printf '%-*s' $(( SIDE_W - 2 )) ' SERVICES'
  reset_cell

  typeset -a svcs=(
    "nfs-server:NFS"
    "rsyncd:rsyncd"
    "smb:Samba"
    "synshare-httpd:HTTP"
    "synshare-tftpd:TFTP"
    "synshare-nc:Netcat"
  )

  local r=1
  for entry in $svcs; do
    local unit="${entry%%:*}" label="${entry##*:}"
    tput_cup $(( SIDE_Y + r + 1 )) $(( SIDE_X + 2 ))
    if svc_active "$unit"; then
      fg256 $C_GREEN; printf '● '; reset_cell
      fg256 $C_WHITE; printf '%-10s' "$label"
      fg256 $C_GREEN; printf ' UP'
    else
      fg256 $C_DIM;   printf '○ '; reset_cell
      fg256 $C_DIM;   printf '%-10s' "$label"
      fg256 $C_DIM;   printf ' --'
    fi
    reset_cell
    (( r++ ))
  done

  # local IPs
  tput_cup $(( SIDE_Y + r + 2 )) $(( SIDE_X + 1 ))
  fg256 $C_YELLOW; tput_bold; printf '%-*s' $(( SIDE_W-2 )) ' IPs'; reset_cell
  local addrs
  addrs=( ${(f)"$(ip -4 addr show | awk '/state UP/{i=1} i&&/inet /{print $2}' | cut -d/ -f1)"} )
  (( r++ ))
  for addr in $addrs; do
    (( r >= SIDE_H - 3 )) && break
    (( r++ ))
    tput_cup $(( SIDE_Y + r + 2 )) $(( SIDE_X + 2 ))
    fg256 $C_CYAN; printf '%-*s' $(( SIDE_W - 4 )) "$addr"; reset_cell
  done
}

# ── Generic list-menu renderer ─────────────────────────────────────────────────
#   draw_menu  title  y x h w  items...
#   Returns selected index in REPLY
draw_menu() {
  local title="$1" my=$2 mx=$3 mh=$4 mw=$5
  shift 5
  local -a items=("$@")
  local nitems=${#items}
  local inner_w=$(( mw - 4 ))
  local cur=0 key

  tput_hide
  while true; do
    draw_box $my $mx $mh $mw $C_ACCENT
    # title bar
    tput_cup $my $(( mx + 1 ))
    set_cell $C_ACCENT $C_BG; tput_bold
    printf ' %-*s' $(( mw - 2 )) "$title"
    reset_cell

    local display_rows=$(( mh - 3 ))
    local scroll=0
    (( cur >= display_rows )) && scroll=$(( cur - display_rows + 1 ))

    local i
    for (( i=0; i<display_rows && i+scroll<nitems; i++ )); do
      local idx=$(( i + scroll ))
      local item="${items[$(( idx + 1 ))]}"
      tput_cup $(( my + i + 2 )) $(( mx + 2 ))
      if (( idx == cur )); then
        set_cell $C_BG $C_ACCENT; tput_bold
        printf '▶ %-*s' $inner_w "$item"
      else
        fg256 $C_WHITE
        printf '  %-*s' $inner_w "$item"
      fi
      reset_cell
    done

    # hint
    tput_cup $(( my + mh - 1 )) $(( mx + 2 ))
    fg256 $C_DIM
    printf '↑↓/jk  Enter  q=back'
    reset_cell

    # read key
    read -k1 -s key
    case "$key" in
      $'\e')
        read -k2 -s -t 0.05 key || true
        case "$key" in
          '[A') (( cur > 0 ))          && (( cur-- )) ;;
          '[B') (( cur < nitems - 1 )) && (( cur++ )) ;;
        esac ;;
      'k'|'K') (( cur > 0 ))          && (( cur-- )) ;;
      'j'|'J') (( cur < nitems - 1 )) && (( cur++ )) ;;
      $'\n'|$'\r') tput_show; REPLY=$cur; return 0 ;;
      'q'|'Q')     tput_show; REPLY=-1;  return 1 ;;
      [0-9])
        local n=$(( key - 1 ))
        (( n >= 0 && n < nitems )) && { tput_show; REPLY=$n; return 0; } ;;
    esac
  done
}

# ── Input prompt (inline) ──────────────────────────────────────────────────────
prompt_input() {
  local label="$1" secret="${2:-0}"
  local py=$(( ROWS / 2 )) px=$(( COLS / 4 ))
  local pw=$(( COLS / 2 ))
  draw_box $py $px 5 $pw $C_YELLOW
  tput_cup $(( py + 1 )) $(( px + 2 ))
  fg256 $C_YELLOW; tput_bold; printf '%s' "$label"; reset_cell
  tput_cup $(( py + 2 )) $(( px + 2 ))
  fg256 $C_WHITE
  tput_show
  if (( secret )); then
    local val
    IFS= read -rs val
    REPLY="$val"
  else
    local val
    IFS= read -r val
    REPLY="$val"
  fi
  tput_hide
  reset_cell
}

# ── Confirm dialog ─────────────────────────────────────────────────────────────
confirm() {
  local msg="$1"
  local py=$(( ROWS / 2 - 1 )) px=$(( (COLS - 50) / 2 ))
  draw_box $py $px 5 50 $C_RED
  tput_cup $(( py + 1 )) $(( px + 2 ))
  fg256 $C_RED; tput_bold; printf '%-46s' "$msg"; reset_cell
  tput_cup $(( py + 2 )) $(( px + 2 ))
  fg256 $C_WHITE; printf 'Press y to confirm, any other key to cancel'
  reset_cell
  tput_show
  read -k1 -s key
  tput_hide
  [[ "$key" == 'y' || "$key" == 'Y' ]]
}

# ── Pager / log viewer ─────────────────────────────────────────────────────────
show_log() {
  tput_clear
  tput_show
  tail -n $(( ROWS - 3 )) "$LOG_FILE" 2>/dev/null || echo "(no log yet)"
  printf '\n[Press any key]'
  read -k1 -s
  tput_hide
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#   SERVER ACTIONS
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

srv_setup_dirs() {
  local d
  for d in "$NFS_DIR" "$RSYNC_DIR" "$SMB_DIR" "$HTTP_DIR" "$TFTP_DIR" "$NC_DEST" "$CONF_DIR"; do
    sudo mkdir -p "$d"
  done
  sudo touch "$LOG_FILE"
  sudo chmod 777 "$NC_DEST" "$RSYNC_DIR"
}

# ── rsync daemon ───────────────────────────────────────────────────────────────
srv_start_rsync() {
  ensure_pkg rsync || return
  srv_setup_dirs

  # Prompt auth if not set
  if [[ -z "$RSYNC_PASS" ]]; then
    prompt_input "rsync password for '${RSYNC_USER}': " 1
    RSYNC_PASS="$REPLY"
  fi
  [[ -z "$RSYNC_PASS" ]] && { status_err "Password required for rsync"; return; }
  setup_rsync_auth

  sudo tee /etc/rsyncd.conf >/dev/null <<EOF
uid = nobody
gid = nobody
use chroot = no
max connections = 16
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

  sudo systemd-run --unit=rsyncd \
    /usr/bin/rsync --daemon --no-detach --port="$RSYNC_PORT" \
    --config=/etc/rsyncd.conf >>"$LOG_FILE" 2>&1 &
  fw_tcp "$RSYNC_PORT"
  status_ok "rsyncd started on :${RSYNC_PORT}  user=${RSYNC_USER}"
  log "rsyncd started"
}

srv_stop_rsync() { sudo systemctl stop rsyncd 2>/dev/null; sudo pkill -f 'rsync --daemon' 2>/dev/null || true; status_ok "rsyncd stopped"; }

# ── Samba ──────────────────────────────────────────────────────────────────────
srv_start_samba() {
  ensure_pkg samba || return
  srv_setup_dirs

  if [[ -z "$SMB_PASS" ]]; then
    prompt_input "Samba password for '${SMB_USER}': " 1
    SMB_PASS="$REPLY"
  fi
  [[ -z "$SMB_PASS" ]] && { status_err "Password required for Samba"; return; }
  setup_smb_auth

  sudo tee /etc/samba/smb.conf >/dev/null <<EOF
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

  sudo systemctl enable --now smb nmb >>"$LOG_FILE" 2>&1
  fw_tcp 139; fw_tcp 445; fw_udp 137; fw_udp 138
  status_ok "Samba started  user=${SMB_USER}  share=\\\\$(hostname -s)\\share"
  log "Samba started"
}

srv_stop_samba() { sudo systemctl disable --now smb nmb >>"$LOG_FILE" 2>&1 || true; status_ok "Samba stopped"; }

# ── NFS ────────────────────────────────────────────────────────────────────────
srv_start_nfs() {
  ensure_pkg nfs-utils || return
  srv_setup_dirs
  sudo chown nobody:nobody "$NFS_DIR"; sudo chmod 0775 "$NFS_DIR"
  grep -qF "$NFS_DIR" /etc/exports 2>/dev/null || \
    printf '%s  %s(rw,sync,no_subtree_check,fsid=0,no_root_squash)\n' \
      "$NFS_DIR" "$SUBNET" | sudo tee -a /etc/exports >/dev/null
  sudo systemctl enable --now nfs-server >>"$LOG_FILE" 2>&1
  sudo exportfs -rav >>"$LOG_FILE" 2>&1
  fw_tcp 2049
  # Optional: wrap with SSH tunnel hint
  status_ok "NFSv4 started  →  mount -t nfs4 $(hostname -s):/ /mnt/..."
  log "NFSv4 started"
}

srv_stop_nfs() {
  sudo systemctl disable --now nfs-server >>"$LOG_FILE" 2>&1 || true
  status_ok "NFSv4 stopped"
}

# ── HTTP (read-only) ───────────────────────────────────────────────────────────
srv_start_http() {
  ensure_pkg busybox || return
  srv_setup_dirs
  sudo systemd-run --unit=synshare-httpd \
    /usr/bin/busybox httpd -f -p "0.0.0.0:${HTTP_PORT}" \
    -h "$HTTP_DIR" >>"$LOG_FILE" 2>&1
  fw_tcp "$HTTP_PORT"
  status_ok "HTTP on :${HTTP_PORT}  root=${HTTP_DIR}  (read-only)"
  log "HTTP started"
}

srv_stop_http() { sudo systemctl stop synshare-httpd 2>/dev/null || true; status_ok "HTTP stopped"; }

# ── TFTP ───────────────────────────────────────────────────────────────────────
srv_start_tftp() {
  ensure_pkg tftp-hpa || return
  srv_setup_dirs
  sudo chown -R nobody:nobody "$TFTP_DIR"; sudo chmod -R 0775 "$TFTP_DIR"
  sudo tee /etc/systemd/system/synshare-tftpd.service >/dev/null <<EOF
[Unit]
Description=SYN-SHARE TFTP
After=network.target
[Service]
ExecStart=/usr/bin/in.tftpd --listen --address 0.0.0.0:69 --secure ${TFTP_DIR}
Restart=on-failure
[Install]
WantedBy=multi-user.target
EOF
  sudo systemctl daemon-reload
  sudo systemctl enable --now synshare-tftpd >>"$LOG_FILE" 2>&1
  fw_udp 69
  status_ok "TFTP started  root=${TFTP_DIR}"
  log "TFTP started"
}

srv_stop_tftp() { sudo systemctl disable --now synshare-tftpd 2>/dev/null || true; status_ok "TFTP stopped"; }

# ── Netcat receiver ────────────────────────────────────────────────────────────
srv_start_nc() {
  ensure_pkg openbsd-netcat || true
  ensure_pkg pv || true
  srv_setup_dirs
  sudo systemd-run --unit=synshare-nc \
    bash -lc "cd '${NC_DEST}' && nc -lvkp ${NC_PORT} | pv | tar xpf -" >>"$LOG_FILE" 2>&1
  fw_tcp "$NC_PORT"
  status_ok "Netcat receiver on :${NC_PORT}  dest=${NC_DEST}"
  log "Netcat receiver started"
}

srv_stop_nc() { sudo systemctl stop synshare-nc 2>/dev/null || true; status_ok "Netcat stopped"; }

# ── OTG quick-start ────────────────────────────────────────────────────────────
srv_otg_start() {
  status_ok "OTG: starting rsync + HTTP …"
  srv_start_rsync
  srv_start_http
  status_ok "OTG ready  •  rsync://${RSYNC_USER}@$(hostname -I | awk '{print $1}'):${RSYNC_PORT}/share  •  http://$(hostname -I | awk '{print $1}'):${HTTP_PORT}"
  log "OTG mode started"
}

# ── Stop all ───────────────────────────────────────────────────────────────────
srv_stop_all() {
  confirm "Stop ALL services?" || { status_ok "Cancelled"; return; }
  srv_stop_rsync; srv_stop_samba; srv_stop_nfs
  srv_stop_http; srv_stop_tftp; srv_stop_nc
  status_ok "All services stopped"
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#   CLIENT ACTIONS
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

typeset -g C_SERVER_IP=""
typeset -g C_RSYNC_PASS=""
typeset -g C_SMB_PASS=""

cli_set_server() {
  prompt_input "Server IP or hostname: "
  [[ -z "$REPLY" ]] && { status_err "No address given"; return; }
  C_SERVER_IP="$REPLY"
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
}

cli_rsync_pull() {
  [[ -z "$C_SERVER_IP" ]] && { status_err "Set server IP first"; return; }
  if [[ -z "$C_RSYNC_PASS" ]]; then
    prompt_input "rsync password: " 1; C_RSYNC_PASS="$REPLY"
  fi
  mkdir -p "$LOCAL_PULL"
  tput_show; tput_clear
  RSYNC_PASSWORD="$C_RSYNC_PASS" \
    rsync -avh --progress --port="$RSYNC_PORT" \
    "rsync://${RSYNC_USER}@${C_SERVER_IP}/share/" "${LOCAL_PULL}/" ; local rc=$?
  tput_hide
  (( rc == 0 )) && status_ok "Pull complete → ${LOCAL_PULL}" || status_err "rsync pull failed (rc=$rc)"
  log "rsync pull from $C_SERVER_IP rc=$rc"
}

cli_rsync_push() {
  [[ -z "$C_SERVER_IP" ]] && { status_err "Set server IP first"; return; }
  prompt_input "Local path to push: "
  local src="$REPLY"
  [[ -z "$src" ]] && return
  if [[ -z "$C_RSYNC_PASS" ]]; then
    prompt_input "rsync password: " 1; C_RSYNC_PASS="$REPLY"
  fi
  tput_show; tput_clear
  RSYNC_PASSWORD="$C_RSYNC_PASS" \
    rsync -avh --progress --port="$RSYNC_PORT" \
    "$src" "rsync://${RSYNC_USER}@${C_SERVER_IP}/share/" ; local rc=$?
  tput_hide
  (( rc == 0 )) && status_ok "Push complete" || status_err "rsync push failed (rc=$rc)"
  log "rsync push to $C_SERVER_IP rc=$rc"
}

cli_smb_mount() {
  [[ -z "$C_SERVER_IP" ]] && { status_err "Set server IP first"; return; }
  if [[ -z "$C_SMB_PASS" ]]; then
    prompt_input "Samba password: " 1; C_SMB_PASS="$REPLY"
  fi
  sudo mkdir -p "$MNT_SMB"
  sudo mount -t cifs "//${C_SERVER_IP}/share" "$MNT_SMB" \
    -o "user=${SMB_USER},password=${C_SMB_PASS},vers=3.0" >>"$LOG_FILE" 2>&1 && \
    status_ok "SMB mounted → ${MNT_SMB}" || status_err "SMB mount failed (check log)"
}

cli_smb_umount() { sudo umount "$MNT_SMB" 2>/dev/null && status_ok "SMB unmounted" || status_err "Not mounted?"; }

cli_nfs_mount() {
  [[ -z "$C_SERVER_IP" ]] && { status_err "Set server IP first"; return; }
  prompt_input "Mount via SSH tunnel? (y/n): "
  if [[ "$REPLY" == 'y' || "$REPLY" == 'Y' ]]; then
    # SSH tunnel: forward NFS port locally
    local tunnel_port="20490"
    ssh -fNL "${tunnel_port}:localhost:2049" "$C_SERVER_IP" >>"$LOG_FILE" 2>&1
    sudo mkdir -p "$MNT_NFS"
    sudo mount -t nfs4 "localhost:/" "$MNT_NFS" -o "port=${tunnel_port}" >>"$LOG_FILE" 2>&1 && \
      status_ok "NFS mounted via SSH tunnel → ${MNT_NFS}" || status_err "NFS mount failed"
  else
    sudo mkdir -p "$MNT_NFS"
    sudo mount -t nfs4 "${C_SERVER_IP}:/" "$MNT_NFS" >>"$LOG_FILE" 2>&1 && \
      status_ok "NFS mounted → ${MNT_NFS}" || status_err "NFS mount failed"
  fi
}

cli_nfs_umount() { sudo umount "$MNT_NFS" 2>/dev/null && status_ok "NFS unmounted" || status_err "Not mounted?"; }

cli_http_mirror() {
  [[ -z "$C_SERVER_IP" ]] && { status_err "Set server IP first"; return; }
  mkdir -p "$LOCAL_PULL"
  tput_show; tput_clear
  wget -m -np -nH --cut-dirs=0 "http://${C_SERVER_IP}:${HTTP_PORT}/" -P "$LOCAL_PULL" ; local rc=$?
  tput_hide
  (( rc == 0 )) && status_ok "HTTP mirror → ${LOCAL_PULL}" || status_err "wget failed (rc=$rc)"
}

cli_tftp_get() {
  [[ -z "$C_SERVER_IP" ]] && { status_err "Set server IP first"; return; }
  prompt_input "Remote filename: "; local f="$REPLY"
  [[ -z "$f" ]] && return
  tput_show; tput_clear
  tftp "$C_SERVER_IP" -m binary -c get "$f"; local rc=$?
  tput_hide
  (( rc == 0 )) && status_ok "TFTP GET → ./${f}" || status_err "TFTP GET failed"
}

cli_tftp_put() {
  [[ -z "$C_SERVER_IP" ]] && { status_err "Set server IP first"; return; }
  prompt_input "Local filename to PUT: "; local f="$REPLY"
  [[ -z "$f" || ! -f "$f" ]] && { status_err "File not found: $f"; return; }
  tput_show; tput_clear
  tftp "$C_SERVER_IP" -m binary -c put "$f"; local rc=$?
  tput_hide
  (( rc == 0 )) && status_ok "TFTP PUT done" || status_err "TFTP PUT failed"
}

cli_nc_send() {
  [[ -z "$C_SERVER_IP" ]] && { status_err "Set server IP first"; return; }
  prompt_input "Local path to send: "; local src="$REPLY"
  [[ -z "$src" || ! -e "$src" ]] && { status_err "Path not found: $src"; return; }
  local dir="${src:h}" base="${src:t}"
  tput_show; tput_clear
  ( cd "$dir" && tar cpf - "$base" ) | pv | nc -q 0 "$C_SERVER_IP" "$NC_PORT"; local rc=$?
  tput_hide
  (( rc == 0 )) && status_ok "Netcat send done" || status_err "Netcat send failed (rc=$rc)"
}

cli_ssh_copy() {
  [[ -z "$C_SERVER_IP" ]] && { status_err "Set server IP first"; return; }
  prompt_input "Local path to SCP: "; local src="$REPLY"
  [[ -z "$src" ]] && return
  prompt_input "Remote destination (user@host:path or just path): "; local dst="$REPLY"
  [[ -z "$dst" ]] && dst="${C_SERVER_IP}:~/syn-share-recv/"
  tput_show; tput_clear
  scp -r "$src" "$dst"; local rc=$?
  tput_hide
  (( rc == 0 )) && status_ok "SCP done" || status_err "SCP failed"
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#   MENU SCREENS
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

MAIN_W=$(( COLS - SIDE_W - 4 ))
MAIN_H=$(( ROWS - 5 ))
MAIN_Y=3
MAIN_X=1

redraw_base() {
  tput_clear
  draw_header
  draw_sidebar
  draw_status
}

# ── Main menu ──────────────────────────────────────────────────────────────────
run_main_menu() {
  local -a items=(
    "  ⚡  OTG Quick Share  (rsync + HTTP, authenticated)"
    "  🖥  SERVER mode  —  manage this machine's services"
    "  💻  CLIENT mode  —  connect to a remote SYN-SHARE host"
    "  📋  View log"
    "  ✗   Quit"
  )
  while true; do
    redraw_base
    draw_menu "SYN-SHARE  ·  Main" $MAIN_Y $MAIN_X $MAIN_H $MAIN_W $items || break
    case $REPLY in
      0) MODE=server; srv_otg_start; sleep 2 ;;
      1) MODE=server; run_server_menu ;;
      2) MODE=client; run_client_menu ;;
      3) show_log ;;
      4) break ;;
    esac
  done
}

# ── Server menu ────────────────────────────────────────────────────────────────
run_server_menu() {
  local -a items=(
    "  ▶ Start  rsync daemon (auth)"
    "  ■ Stop   rsync daemon"
    "  ▶ Start  Samba (auth)"
    "  ■ Stop   Samba"
    "  ▶ Start  NFSv4"
    "  ■ Stop   NFSv4"
    "  ▶ Start  HTTP  (read-only :${HTTP_PORT})"
    "  ■ Stop   HTTP"
    "  ▶ Start  TFTP"
    "  ■ Stop   TFTP"
    "  ▶ Start  Netcat receiver"
    "  ■ Stop   Netcat"
    "  ✗ Stop ALL services"
    "  ← Back"
  )
  while true; do
    redraw_base
    draw_menu "SERVER control" $MAIN_Y $MAIN_X $MAIN_H $MAIN_W $items || return
    case $REPLY in
      0)  srv_start_rsync ;;
      1)  srv_stop_rsync ;;
      2)  srv_start_samba ;;
      3)  srv_stop_samba ;;
      4)  srv_start_nfs ;;
      5)  srv_stop_nfs ;;
      6)  srv_start_http ;;
      7)  srv_stop_http ;;
      8)  srv_start_tftp ;;
      9)  srv_stop_tftp ;;
      10) srv_start_nc ;;
      11) srv_stop_nc ;;
      12) srv_stop_all ;;
      13) return ;;
    esac
    draw_sidebar   # refresh status column
    draw_status
  done
}

# ── Client menu ────────────────────────────────────────────────────────────────
run_client_menu() {
  local -a items=(
    "  🔍 Set / probe server  (current: ${C_SERVER_IP:-none})"
    "  ↓  rsync PULL  → ${LOCAL_PULL}"
    "  ↑  rsync PUSH  ← local path"
    "  🔗 Mount Samba share"
    "  ✂  Unmount Samba"
    "  🔗 Mount NFS (with SSH tunnel option)"
    "  ✂  Unmount NFS"
    "  🌐 HTTP mirror (wget)"
    "  ↓  TFTP GET"
    "  ↑  TFTP PUT"
    "  📡 Netcat SEND dir/file"
    "  🔒 SCP (SSH copy)"
    "  ← Back"
  )
  while true; do
    # refresh server label
    items[1]="  🔍 Set / probe server  (current: ${C_SERVER_IP:-none})"
    redraw_base
    draw_menu "CLIENT  →  ${C_SERVER_IP:-<no server>}" $MAIN_Y $MAIN_X $MAIN_H $MAIN_W $items || return
    case $REPLY in
      0)  cli_set_server ;;
      1)  cli_rsync_pull ;;
      2)  cli_rsync_push ;;
      3)  cli_smb_mount ;;
      4)  cli_smb_umount ;;
      5)  cli_nfs_mount ;;
      6)  cli_nfs_umount ;;
      7)  cli_http_mirror ;;
      8)  cli_tftp_get ;;
      9)  cli_tftp_put ;;
      10) cli_nc_send ;;
      11) cli_ssh_copy ;;
      12) return ;;
    esac
    draw_status
  done
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
#   ENTRYPOINT
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

cleanup() {
  tput_show
  tput_clear
  tput_reset
  print "Bye from SYN-SHARE."
}
trap cleanup EXIT INT TERM

# Resize handler
trap 'COLS=$(tput cols); ROWS=$(tput lines); SIDE_X=$(( COLS - 24 )); MAIN_W=$(( COLS - SIDE_W - 4 )); redraw_base' WINCH

# Validate terminal
if [[ ! -t 0 || ! -t 1 ]]; then
  print "SYN-SHARE requires an interactive terminal." >&2
  exit 1
fi

tput_hide
tput_clear

# Check for sudo availability (not mandatory at launch for client-only use)
if ! command -v sudo >/dev/null 2>&1 && ! is_root; then
  status_err "sudo not found — server operations will fail without it"
fi

log "SYN-SHARE started (euid=$EUID)"
run_main_menu
