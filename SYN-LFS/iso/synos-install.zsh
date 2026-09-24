#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                       S Y N O S - I N S T A L L
#
#   synos-install on the SYN-OS live ISO. The whole system is already on
#   the ISO, built, so nothing is downloaded: partition the disk named in
#   synos.conf (EFI system partition + ext4 root, with the GPT labels the
#   kernel boots by), copy the clean root image onto it, put the kernel
#   where UEFI firmware looks for a boot file, then set up the user,
#   hostname, time zone, keyboard and locale from synos.conf.
#
#   Where the answers come from, in this order:
#     synos-install FILE      a synos.conf given on the command line
#     a punchset              a medium labelled SYNPUNCH with synos.conf at
#                             its root: plug it in, the install reads it
#     /etc/syn-os/synos.conf  the ISO's own copy
#   Whatever the file leaves at CHANGE_ME (Disk, the password) is asked
#   for on the terminal; with a full punchset and RequireWipeConfirm=no
#   nothing is asked at all.
#   Looks and logs like SYN-OS-X's installer: same syn-ui.zsh, same
#   synos.conf, same script(1) log.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYNOS-INSTALL (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

Source=/run/syn/rootfs      # the ISO's root image, mounted by the live init
Target=/mnt/syn-target
Punch=/run/syn/punch        # where a punchset is mounted, read-only

source /usr/lib/syn-os/syn-ui.zsh
fail() { syn_ui::error "$1"; exit 1; }

# --- The answers: a file on the command line, a punchset, or the ISO's own ----
PunchDev=
if [ -n "${1:-}" ]; then
  Conf=$1
  [ -f "$Conf" ] || fail "No such file: $Conf"
else
  PunchDev=$(blkid -c /dev/null -o device -t LABEL=SYNPUNCH 2>/dev/null | head -n1 || true)
  if [ -n "$PunchDev" ]; then
    mkdir -p "$Punch"
    mountpoint -q "$Punch" || mount -o ro "$PunchDev" "$Punch" || fail "Cannot mount the punchset on $PunchDev"
    [ -f "$Punch/synos.conf" ] || fail "$PunchDev is labelled SYNPUNCH but has no synos.conf at its root"
    Conf=$Punch/synos.conf
  else
    Conf=/etc/syn-os/synos.conf
  fi
fi
source "$Conf"

# The whole run goes to a log via script(1), as syn-stage0.zsh does it
InstallLog="/root/synos-install-$(date +%Y%m%d-%H%M%S).log"
if [ -z "${SYN_INSTALL_UNDER_SCRIPT:-}" ]; then
  export SYN_INSTALL_UNDER_SCRIPT=1
  exec script -qefc "$0 $*" "$InstallLog"
fi
syn_ui::info "Logging full install output to ${InstallLog}"
if [ -n "$PunchDev" ]; then
  syn_ui::info "Answers from the punchset on ${PunchDev}"
else
  syn_ui::info "Answers from ${Conf}"
fi

cleanup() {
  mountpoint -q "$Punch" 2>/dev/null && umount "$Punch" 2>/dev/null || true
  [ -n "${SynInstallComplete:-}" ] && return
  umount -R "$Target" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# --- Whatever the answers leave at CHANGE_ME is asked for here ----------------
ask() {
  local answer
  printf "%s%s%s " "$C_VALUE" "$1" "$RESET"
  read -r answer </dev/tty
  print -r -- "$answer"
}
if [ "${Disk:-CHANGE_ME}" = CHANGE_ME ]; then
  printf "\n"
  lsblk -d -o NAME,SIZE,MODEL,TRAN 2>/dev/null | sed 's/^/  /'
  Disk=$(ask "Install to which disk (a name from the list, or /dev/...)?")
  [[ $Disk == /dev/* ]] || Disk=/dev/$Disk
fi
if [ "${UserAccountPassword:-CHANGE_ME}" = CHANGE_ME ]; then
  while :; do
    printf "%sPassword for %s:%s " "$C_VALUE" "${UserAccountName:-the user}" "$RESET"
    read -rs UserAccountPassword </dev/tty; printf "\n"
    printf "%sAgain:%s " "$C_VALUE" "$RESET"
    read -rs Again </dev/tty; printf "\n"
    [ -n "$UserAccountPassword" ] && [ "$UserAccountPassword" = "$Again" ] && break
    syn_ui::error "Empty, or the two did not match. Once more."
  done
  unset Again
fi

# --- Checks, before anything touches a disk -----------------------------------
[ -d /sys/firmware/efi ] ||
  fail "Booted in BIOS mode. SYN-OS installs for UEFI only: boot the ISO in UEFI mode."
[ -d "$Source/usr" ] ||
  fail "No root image at $Source. synos-install runs from the SYN-OS ISO."
[ -b "$Disk" ] || fail "Disk=$Disk is not a block device."
[ -n "${UserAccountName:-}" ] || fail "Set UserAccountName in $Conf."
Medium=$(findmnt -no SOURCE /run/syn/medium 2>/dev/null || true)
[[ -n $Medium && $Medium == ${Disk}* ]] && fail "$Disk is the disk this ISO is running from."
[[ -n $PunchDev && $PunchDev == ${Disk}* ]] && fail "$Disk holds the punchset."

if [ "${RequireWipeConfirm:-yes}" = "yes" ]; then
  syn_ui::confirm_wipe "$Disk" || fail "Aborted — disk not confirmed."
fi

syn_ui::face
loadkeys "$KeyMap" 2>/dev/null || true
setfont "$VconsoleFont" 2>/dev/null || true
syn_ui::intro_montage

# Partition device names: /dev/sda1, but /dev/nvme0n1p1 and /dev/mmcblk0p1
part() { [[ $Disk == *[0-9] ]] && print -r -- "${Disk}p$1" || print -r -- "${Disk}$1"; }
EspPart=$(part 1)
RootPart=$(part 2)

# --- Disk ---------------------------------------------------------------------
syn_ui::step "Partitioning ${Disk} (GPT: ${BootSize} EFI system, root on the rest)"
umount -R "$Target" 2>/dev/null || true
wipefs -a "$Disk"
sfdisk --wipe always "$Disk" <<EOF
label: gpt
size=${BootSize}, type=U, name=SYNEFI
type=L, name=synroot
EOF
udevadm settle
for i in {1..10}; do [ -b "$RootPart" ] && break; sleep 1; done
[ -b "$RootPart" ] || fail "$RootPart did not appear after partitioning."
syn_ui::step_done "Partitioned"

syn_ui::step "Creating filesystems"
mkfs.vfat -F 32 -n SYNEFI "$EspPart"
mkfs.ext4 -F -q -L synroot "$RootPart"
mkdir -p "$Target"
mount "$RootPart" "$Target"
mkdir -p "$Target/boot/efi"
mount "$EspPart" "$Target/boot/efi"
syn_ui::step_done "FAT32 EFI system partition on ${EspPart}, ext4 root on ${RootPart}"

# --- System -------------------------------------------------------------------
syn_ui::step "Copying SYN-OS onto ${RootPart} (several GB, give it a few minutes)"
cp -a "$Source/." "$Target/"
syn_ui::step_done "System copied"

syn_ui::step "Installing the kernel as the firmware's boot file"
Kernel=( "$Target"/boot/vmlinuz-*(N) )
(( $#Kernel )) || fail "No kernel in $Target/boot."
install -Dm644 "${Kernel[1]}" "$Target/boot/efi/EFI/BOOT/BOOTX64.EFI"
syn_ui::step_done "EFI/BOOT/BOOTX64.EFI (${Kernel[1]:t})"

# --- Settings from synos.conf ---------------------------------------------------
syn_ui::step "Configuring ${Hostname}"
print -r -- "$Hostname" > "$Target/etc/hostname"
if [ -e "$Target/usr/share/zoneinfo/$TimeZone" ]; then
  ln -sf "/usr/share/zoneinfo/$TimeZone" "$Target/etc/localtime"
else
  syn_ui::error "No time zone called $TimeZone, using Europe/London"
  ln -sf /usr/share/zoneinfo/Europe/London "$Target/etc/localtime"
fi
printf 'KEYMAP="%s"\nFONT="%s"\n' "$KeyMap" "$VconsoleFont" > "$Target/etc/sysconfig/console"

# labwc reads its layout from XKB_DEFAULT_LAYOUT; console keymap and XKB
# names differ for some layouts (uk -> gb). Set in skel, before the user
# is created from it
case "$KeyMap" in
  uk) XkbLayout="gb" ;;
  *)  XkbLayout="$KeyMap" ;;
esac
LabwcEnv="$Target/etc/skel/.config/labwc/environment"
if [ -f "$LabwcEnv" ]; then
  sed -i '/^XKB_DEFAULT_LAYOUT=/d' "$LabwcEnv"
  print -r -- "XKB_DEFAULT_LAYOUT=${XkbLayout}" >> "$LabwcEnv"
fi

# Locale: set LANG for logins, and generate it if the system lacks it
print -r -- "export LANG=${Locale}" > "$Target/etc/profile.d/syn-locale.sh"
WantLocale=${${Locale:l}//-/}
if ! chroot "$Target" locale -a 2>/dev/null | tr 'A-Z' 'a-z' | tr -d '-' | grep -qx "$WantLocale"; then
  chroot "$Target" localedef -i "${Locale%%.*}" -f "${Locale#*.}" "$Locale" ||
    syn_ui::error "Could not generate ${Locale}; fix it later with localedef"
fi

# Every install gets its own machine id, not the build image's
rm -f "$Target/var/lib/dbus/machine-id"
dbus-uuidgen --ensure="$Target/var/lib/dbus/machine-id"
syn_ui::step_done "Hostname, time zone (${TimeZone}), keyboard (${KeyMap}/${XkbLayout}), locale (${Locale})"

# --- User -------------------------------------------------------------------------
syn_ui::step "Creating ${UserAccountName}"
useradd -R "$Target" -m -G wheel,video,audio,input -s "${UserShell:-/bin/zsh}" "$UserAccountName"
print -r -- "${UserAccountName}:${UserAccountPassword}" | chpasswd -R "$Target"
# root stays locked: the wheel group has doas
passwd -R "$Target" -l root > /dev/null

# This install's own take on every theme's wallpaper, as Stage 1 does it
UserEntry=$(grep "^${UserAccountName}:" "$Target/etc/passwd")
UserUid=$(print -r -- "$UserEntry" | cut -d: -f3)
UserGid=$(print -r -- "$UserEntry" | cut -d: -f4)
UserHome="$Target$(print -r -- "$UserEntry" | cut -d: -f6)"
if [ -x /usr/lib/syn-os/syn-wallgen ] && [ -d "$UserHome/.config/syn-os/themes" ]; then
  /usr/lib/syn-os/syn-wallgen --themes-dir "$UserHome/.config/syn-os/themes" --out-dir "$UserHome/.wallpaper" &&
    chown -R "$UserUid:$UserGid" "$UserHome/.wallpaper" ||
    syn_ui::error "syn-wallgen failed; the stock wallpapers are still there"
fi
syn_ui::step_done "${UserAccountName} (groups wheel, video, audio, input)"

# The answers go with the install, minus the password, and with the disk
# it went on
sed -e '/^UserAccountPassword=/d' -e "s|^Disk=.*|Disk=\"$Disk\"|" "$Conf" > "$Target/etc/syn-os/synos.conf"

# --- Done -----------------------------------------------------------------------
cp -f "$InstallLog" "$Target/var/log/" 2>/dev/null || true
sync
umount -R "$Target"
SynInstallComplete=1

syn_ui::final_banner
syn_ui::info "Take the installation medium out, then: reboot"
syn_ui::info "Log in as ${UserAccountName} and type synos for the desktop."
