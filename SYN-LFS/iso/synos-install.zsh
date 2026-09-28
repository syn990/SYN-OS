#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#   synos-install [FILE], SYN-OS on runit
#
#   Settles the answers, then runs the same installer as the Arch edition:
#   syn-stage0.zsh (the disk: partitions, LUKS, LVM, filesystems; then the
#   system) and syn-stage1.zsh (the new system's setup and bootloader),
#   driven by /etc/syn-os/synos.conf.
#
#   The answers come from the first of: FILE, a punchset (a medium labelled
#   SYNPUNCH with synos.conf at its root), or the ISO's own synos.conf
#   (synos-config edits it). Whatever they leave at CHANGE_ME (the disk, the
#   password, the LUKS passphrase if Encryption=yes) is asked for here.
#   The settled answers go to /etc/syn-os/synos.conf on the live session,
#   where the installer reads them.
# ------------------------------------------------------------------------------
set -euo pipefail
source /usr/lib/syn-os/syn-ui.zsh
fail() { syn_ui::error "$1"; exit 1; }
Punch=/run/syn/punch
Conf=/etc/syn-os/synos.conf

# --- The answers ---------------------------------------------------------------
PunchDev=
if [ -n "${1:-}" ]; then
  [ -f "$1" ] || fail "No such file: $1"
  [ "${1:A}" = "${Conf:A}" ] || cp -f "$1" "$Conf"
  syn_ui::info "Answers from $1"
else
  PunchDev=$(blkid -c /dev/null -o device -t LABEL=SYNPUNCH 2>/dev/null | head -n1 || true)
  if [ -n "$PunchDev" ]; then
    mkdir -p "$Punch"
    mountpoint -q "$Punch" || mount -o ro "$PunchDev" "$Punch" || fail "Cannot mount the punchset on $PunchDev"
    [ -f "$Punch/synos.conf" ] || fail "$PunchDev is labelled SYNPUNCH but has no synos.conf at its root"
    cp -f "$Punch/synos.conf" "$Conf"
    umount "$Punch"
    syn_ui::info "Answers from the punchset on ${PunchDev}"
  fi
fi
chmod 600 "$Conf"
source "$Conf"

# --- Whatever they leave at CHANGE_ME is asked for here -------------------------
# The prompts go to the terminal: the answers are captured by $(...)
ask() {
  local answer
  printf "%s%s%s " "$C_VALUE" "$1" "$RESET" > /dev/tty
  read -r answer </dev/tty
  print -r -- "$answer"
}
askTwice() {
  local one two
  while :; do
    printf "%s%s:%s " "$C_VALUE" "$1" "$RESET" > /dev/tty; read -rs one </dev/tty; printf "\n" > /dev/tty
    printf "%sAgain:%s " "$C_VALUE" "$RESET" > /dev/tty; read -rs two </dev/tty; printf "\n" > /dev/tty
    [ -n "$one" ] && [ "$one" = "$two" ] && { print -r -- "$one"; return; }
    syn_ui::error "Empty, or the two did not match. Once more." > /dev/tty
  done
}
setAnswer() { print -r -- "$1=\"$2\"" >> "$Conf"; }   # the last setting wins

if [ "${Disk:-CHANGE_ME}" = CHANGE_ME ]; then
  printf "\n" > /dev/tty
  lsblk -d -o NAME,SIZE,MODEL,TRAN 2>/dev/null | sed 's/^/  /' > /dev/tty
  Disk=$(ask "Install to which disk (a name from the list, or /dev/...)?")
  [[ $Disk == /dev/* ]] || Disk=/dev/$Disk
  setAnswer Disk "$Disk"
fi
if [ "${UserAccountPassword:-CHANGE_ME}" = CHANGE_ME ]; then
  setAnswer UserAccountPassword "$(askTwice "Password for ${UserAccountName:-the user}")"
fi
case "${Encryption:-no}" in
  y|Y|yes|YES|Yes|true|1)
    if [ "${LuksPassphrase:-CHANGE_ME}" = CHANGE_ME ]; then
      setAnswer LuksPassphrase "$(askTwice "Passphrase to unlock the disk at boot")"
    fi ;;
esac

# --- Checks before anything touches a disk ----------------------------------------
[ -d /run/syn/rootfs/usr ] || fail "No root image at /run/syn/rootfs: synos-install runs from the SYN-OS ISO."
[ -b "$Disk" ] || fail "Disk=$Disk is not a block device."
Medium=$(findmnt -no SOURCE /run/syn/medium 2>/dev/null || true)
[[ -n $Medium && $Medium == ${Disk}* ]] && fail "$Disk is the disk this ISO is running from."
[[ -n $PunchDev && $PunchDev == ${Disk}* ]] && fail "$Disk holds the punchset."

exec zsh /usr/lib/syn-os/syn-stage0.zsh
