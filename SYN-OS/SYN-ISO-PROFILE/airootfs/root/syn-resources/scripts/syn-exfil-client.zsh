# ------------------------------------------------------------------------------
#                  S Y N  –  E X F I L   (Client)
#
#   Lightweight client utility for probing SYN‑EXFIL server endpoints and
#   performing fast push/pull actions. Automatically detects active services
#   (NFSv4, rsyncd, Samba, HTTP, TFTP, Netcat) and exposes an interactive menu
#   for simple, predictable file transfer operations on any SYN‑OS host.
#
#   SYN‑OS        : The Syntax Operating System
#   Component     : SYN‑EXFIL (Client Utility)
#   Author        : William Hayward-Holland (Syntax990)
#   License       : MIT License
# ------------------------------------------------------------------------------

emulate -L zsh
set -e
set -u
(set -o | grep -q pipefail 2>/dev/null && set -o pipefail) || setopt pipefail 2>/dev/null || true
autoload -Uz colors; colors

print_banner() {
  local B=$fg_bold[blue] G=$fg_bold[green] Y=$fg_bold[yellow] C=$fg_bold[cyan] N=$reset_color
  print -P "${G}S Y N – E X F I L   ${Y}(Client)   ${N}|  ${C}Auto‑probe • Push/Pull menu%f"
  print -P ""
}

typeset -g MNT_NFS="/mnt/syn_exfil_nfs"
typeset -g MNT_SMB="/mnt/syn_exfil_smb"
typeset -g LOCAL_PULL_DIR="$HOME/pull_syn_exfil"
typeset -g HTTP_PORT="8080"
typeset -g NC_PORT="7000"

need_root_hint() {
  if [[ $EUID -ne 0 ]]; then
    print -P "%F{yellow}-> Mount operations may require root. Consider: sudo $0%f"
  fi
}

install_client_tools() {
  local pkgs=( nfs-utils cifs-utils rsync wget curl tftp-hpa pv openbsd-netcat )
  for p in $pkgs; do
    if ! pacman -Qi "$p" >/dev/null 2>&1; then
      print -P "%F{yellow}Installing%f $p"
      sudo pacman -Sy --noconfirm --needed "$p" || true
    fi
  done
}

tcp_open() {
  local host="$1" port="$2"
  if command -v nc >/dev/null 2>&1; then
    nc -z -w1 "$host" "$port" >/dev/null 2>&1
  else
    # fallback using /dev/tcp if enabled
    (exec 3<>/dev/tcp/${host}/${port}) 2>/dev/null
  fi
}

udp_open_quick() {
  local host="$1" port="$2"
  command -v nc >/dev/null 2>&1 && nc -uz -w1 "$host" "$port" >/dev/null 2>&1
}

typeset -gi HAS_NFS=0 HAS_RSYNC=0 HAS_SMB=0 HAS_HTTP=0 HAS_TFTP=0 HAS_NC=0

probe_services() {
  local ip="$1"
  HAS_NFS=0 HAS_RSYNC=0 HAS_SMB=0 HAS_HTTP=0 HAS_TFTP=0 HAS_NC=0

  tcp_open "$ip" 2049 && HAS_NFS=1
  tcp_open "$ip" 873  && HAS_RSYNC=1
  tcp_open "$ip" 445  && HAS_SMB=1
  tcp_open "$ip" "$HTTP_PORT" && HAS_HTTP=1
  udp_open_quick "$ip" 69 && HAS_TFTP=1
  tcp_open "$ip" "$NC_PORT" && HAS_NC=1
}

do_nfs_mount()    { local ip="$1"; sudo mkdir -p "$MNT_NFS"; sudo mount -t nfs4 "${ip}:/" "$MNT_NFS"; print "Mounted NFS -> $MNT_NFS"; }
do_nfs_unmount()  { sudo umount "$MNT_NFS" || true }

do_rsync_pull()   { local ip="$1"; mkdir -p "$LOCAL_PULL_DIR"; rsync -avh --progress "rsync://${ip}/share/" "${LOCAL_PULL_DIR}/"; }
do_rsync_push()   { local ip="$1" src="$2"; rsync -avh --progress "$src" "rsync://${ip}/share/"; }

do_smb_mount()    { local ip="$1"; sudo mkdir -p "$MNT_SMB"; sudo mount -t cifs "//${ip}/fastshare" "$MNT_SMB" -o guest,vers=3.0; print "Mounted SMB -> $MNT_SMB"; }
do_smb_unmount()  { sudo umount "$MNT_SMB" || true }

do_http_pull_all(){ local ip="$1"; mkdir -p "$LOCAL_PULL_DIR"; (cd "$LOCAL_PULL_DIR" && wget -m -np -nH --cut-dirs=0 "http://${ip}:${HTTP_PORT}/"); }

do_tftp_get()     { local ip="$1" file="$2"; tftp "$ip" -m binary -c get "$file"; }
do_tftp_put()     { local ip="$1" file="$2"; tftp "$ip" -m binary -c put "$file"; }

do_nc_send_dir()  { local ip="$1" port="$2" src="$3"; local base dir; dir="${src:h}"; base="${src:t}"; (cd "$dir" && tar cpf - "$base") | pv | nc -q 0 "$ip" "$port"; }

client_menu() {
  print_banner
  local SERVER_IP
  read -r "SERVER_IP?Server IP: "
  [[ -z "${SERVER_IP:-}" ]] && { print -P "%F{red}No IP provided.%f"; exit 1; }

  install_client_tools
  probe_services "$SERVER_IP"

  print -P "%F{cyan}Detected on ${SERVER_IP}:%f"
  (( HAS_NFS  )) && print " - NFSv4 (2049/tcp)"
  (( HAS_RSYNC)) && print " - rsyncd (873/tcp)"
  (( HAS_SMB  )) && print " - SMB (445/tcp)"
  (( HAS_HTTP )) && print " - HTTP (${HTTP_PORT}/tcp)"
  (( HAS_TFTP )) && print " - TFTP (69/udp)"
  (( HAS_NC   )) && print " - Netcat receiver (${NC_PORT}/tcp)"
  print

  need_root_hint

  while true; do
    print -P "%F{magenta}===== Actions =====%f"
    typeset -A map
    typeset -i idx=1

    if (( HAS_NFS )); then
      print "$idx) NFS: mount -> ${MNT_NFS}"; map[$idx]=nfs_mount; ((idx++))
      print "$idx) NFS: unmount";            map[$idx]=nfs_umount; ((idx++))
    fi
    if (( HAS_RSYNC )); then
      print "$idx) rsync: PULL server:/share -> ${LOCAL_PULL_DIR}"; map[$idx]=rsync_pull; ((idx++))
      print "$idx) rsync: PUSH <local_path> -> server:/share";      map[$idx]=rsync_push; ((idx++))
    fi
    if (( HAS_SMB )); then
      print "$idx) SMB: mount -> ${MNT_SMB}"; map[$idx]=smb_mount; ((idx++))
      print "$idx) SMB: unmount";             map[$idx]=smb_umount; ((idx++))
    fi
    if (( HAS_HTTP )); then
      print "$idx) HTTP: mirror everything -> ${LOCAL_PULL_DIR}"; map[$idx]=http_pull_all; ((idx++))
    fi
    if (( HAS_TFTP )); then
      print "$idx) TFTP: GET file"; map[$idx]=tftp_get; ((idx++))
      print "$idx) TFTP: PUT file"; map[$idx]=tftp_put; ((idx++))
    fi
    if (( HAS_NC )); then
      print "$idx) Netcat: SEND directory/file -> server:${NC_PORT}"; map[$idx]=nc_send; ((idx++))
    fi
    print "$idx) Re-probe services"; map[$idx]=reprobe; ((idx++))
    print "$idx) Quit";              map[$idx]=quit

    local choice
    read -r "choice?Select: "
    local action="${map[$choice]-invalid}"

    case "$action" in
      nfs_mount)  do_nfs_mount "$SERVER_IP" ;;
      nfs_umount) do_nfs_unmount ;;
      rsync_pull) do_rsync_pull "$SERVER_IP" ;;
      rsync_push) local LP; read -r "LP?Local path to push: "; [[ -z "${LP:-}" ]] || do_rsync_push "$SERVER_IP" "$LP" ;;
      smb_mount)  do_smb_mount "$SERVER_IP" ;;
      smb_umount) do_smb_unmount ;;
      http_pull_all) do_http_pull_all "$SERVER_IP" ;;
      tftp_get)   local RF; read -r "RF?Remote filename to GET: "; [[ -z "${RF:-}" ]] || do_tftp_get "$SERVER_IP" "$RF" ;;
      tftp_put)   local LF; read -r "LF?Local filename to PUT: ";  [[ -z "${LF:-}" ]] || do_tftp_put "$SERVER_IP" "$LF" ;;
      nc_send)    local SRC; read -r "SRC?Local directory or file to SEND: "; [[ -z "${SRC:-}" ]] || do_nc_send_dir "$SERVER_IP" "$NC_PORT" "$SRC" ;;
      reprobe)    probe_services "$SERVER_IP" ;;
      quit)       exit 0 ;;
      *)          print -P "%F{red}Invalid choice.%f" ;;
    esac
    print
  done
}

client_menu
