#!/bin/zsh

# =============================================================================
#                        SYN-OS Pacman Relookup Script
#
# Purpose:
#   Fix common pacman PGP signature errors by rebuilding and refreshing the
#   system keyring. Can optionally refresh mirrors and perform a deep reset.
#
# Context:
#   Safe to run in the live ISO or inside the installed system. Requires
#   working network access. This script focuses on the Arch keyring and does
#   not pin versions or use custom repos.
#
# Guidance:
#   Keep config in config files. This script is procedural by design so it can
#   diagnose and repair broken trust databases in a predictable order.
#
# Options:
#   -r, --reset          Remove /etc/pacman.d/gnupg before reinitialising
#   -m, --mirrors        Refresh pacman mirrors with reflector first
#   -k, --keyserver URL  Set a specific HKP keyserver for refresh step
#   -h, --help           Show usage
#
# Notes on mirrors:
#   Package availability depends on the current pacman mirrorlist at the time
#   you run this script. Everything is vanilla Arch. Using reflector only
#   updates the mirrorlist and does not change package definitions.
#
# Meta:
#   SYN-OS      : The Syntax Operating System
#   Author      : William Hayward-Holland (Syntax990)
#   License     : MIT License
# =============================================================================

set -u

# ------------------------------- CLI parsing -------------------------------- #
DEEP_RESET=0
REFRESH_MIRRORS=0
KEYSERVER=""

usage() {
  cat <<'USAGE'
Usage: syn-pacman-relookup.zsh [options]

Options:
  -r, --reset          Remove /etc/pacman.d/gnupg and rebuild from scratch
  -m, --mirrors        Refresh pacman mirrors using reflector before key steps
  -k, --keyserver URL  Use a specific HKP keyserver for key refresh
  -h, --help           Show this help

Examples:
  syn-pacman-relookup.zsh
  syn-pacman-relookup.zsh --mirrors
  syn-pacman-relookup.zsh --reset --keyserver hkps://keyserver.ubuntu.com
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -r|--reset)   DEEP_RESET=1 ;;
    -m|--mirrors) REFRESH_MIRRORS=1 ;;
    -k|--keyserver)
      shift
      [[ $# -gt 0 ]] || { echo "Missing argument for --keyserver" >&2; exit 2; }
      KEYSERVER="$1"
      ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage; exit 2 ;;
  esac
  shift
done

# ------------------------------- Helpers ------------------------------------ #
check_success() {
  if [[ ${1:-0} -ne 0 ]]; then
    printf "\033[1;31mError: %s\033[0m\n" "${2:-command failed}" >&2
    exit 1
  fi
}

run() {
  # run "desc" cmd...
  local desc="$1"; shift
  echo "$desc"
  "$@"
  check_success $? "$desc"
}

ensure_pkg() {
  # ensure_pkg pkg1 pkg2 ...
  if [[ $# -gt 0 ]]; then
    pacman -Sy --needed --noconfirm "$@"
    check_success $? "Installing packages: $*"
  fi
}

set_keyserver_if_requested() {
  if [[ -n "$KEYSERVER" ]]; then
    echo "Configuring keyserver: $KEYSERVER"
    mkdir -p /etc/pacman.d/gnupg
    echo "keyserver $KEYSERVER" > /etc/pacman.d/gnupg/dirmngr.conf
    # Restart dirmngr used by pacman-key
    gpgconf --kill dirmngr >/dev/null 2>&1 || true
  fi
}

refresh_mirrors_if_requested() {
  if [[ $REFRESH_MIRRORS -eq 1 ]]; then
    echo "Refreshing mirrors with reflector"
    ensure_pkg reflector
    # Keep it conservative: https, recent, sorted by rate
    reflector --protocol https --latest 20 --sort rate --save /etc/pacman.d/mirrorlist
    check_success $? "Refreshing mirrors with reflector"
  fi
}

maybe_deep_reset() {
  if [[ $DEEP_RESET -eq 1 ]]; then
    echo "Deep reset: removing /etc/pacman.d/gnupg"
    rm -rf /etc/pacman.d/gnupg
  fi
}

# ------------------------------- Main flow ---------------------------------- #
echo "SYN-OS Pacman relookup starting..."

# 1) Optional mirror refresh first, so subsequent steps can reach fast mirrors
refresh_mirrors_if_requested

# 2) Make sure gnupg and keyring tools are available
ensure_pkg gnupg archlinux-keyring

# 3) Optional deep reset of the gnupg directory
maybe_deep_reset

# 4) Initialise and populate keyring
run "Initialising pacman keyring..." pacman-key --init
run "Populating default Arch Linux keys..." pacman-key --populate archlinux

# 5) Update the packaged keyring from repos
run "Synchronising databases and updating archlinux-keyring..." pacman -Sy --noconfirm archlinux-keyring

# 6) Set keyserver if provided, then refresh keys
set_keyserver_if_requested
run "Refreshing pacman keys..." pacman-key --refresh-keys

echo "Pacman keyring relookup complete."
