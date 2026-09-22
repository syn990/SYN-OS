#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                       S Y N - L F S   I N S T A L L
#
#   synos-install on the SYN-LFS live ISO. The whole system is already on
#   the ISO, built, so nothing is downloaded: partition the disk named in
#   synos.conf (EFI system partition + ext4 root, with the GPT labels the
#   kernel boots by), copy the clean root image onto it, put the kernel
#   where UEFI firmware looks for a boot file, then set up the user,
#   hostname, time zone, keyboard and locale from synos.conf.
#   Looks and logs like the Arch installer: same syn-ui.zsh, same
#   synos.conf, same script(1) log.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-LFS-INSTALL (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

Conf=/etc/syn-os/synos.conf
Source=/run/syn/rootfs      # the ISO's root image, mounted by the live init
Target=/mnt/syn-target

source /usr/lib/syn-os/syn-ui.zsh
source "$Conf"

# The whole run goes to a log via script(1), as syn-stage0.zsh does it
InstallLog="/root/synos-install-$(date +%Y%m%d-%H%M%S).log"
if [ -z "${SYN_INSTALL_UNDER_SCRIPT:-}" ]; then
  export SYN_INSTALL_UNDER_SCRIPT=1
  exec script -qefc "$0 $*" "$InstallLog"
fi
syn_ui::info "Logging full install output to ${InstallLog}"

fail() { syn_ui::error "$1"; exit 1; }

cleanup() {
  [ -n "${SynInstallComplete:-}" ] && return
  umount -R "$Target" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

# --- Checks, before anything touches a disk -----------------------------------
[ -d /sys/firmware/efi ] ||
  fail "Booted in BIOS mode. SYN-LFS installs for UEFI only: boot the ISO in UEFI mode."
[ -d "$Source/usr" ] ||
  fail "No root image at $Source. synos-install runs from the SYN-LFS ISO."
[ "${Disk:-CHANGE_ME}" != CHANGE_ME ] || fail "Set Disk in $Conf (lsblk lists the disks)."
[ -b "$Disk" ] || fail "Disk=$Disk is not a block device."
[ -n "${UserAccountName:-}" ] || fail "Set UserAccountName in $Conf."
[ "${UserAccountPassword:-CHANGE_ME}" != CHANGE_ME ] || fail "Set UserAccountPassword in $Conf."
Medium=$(findmnt -no SOURCE /run/syn/medium 2>/dev/null || true)
[[ -n $Medium && $Medium == ${Disk}* ]] && fail "$Disk is the disk this ISO is running from."

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
syn_ui::step "Copying SYN-LFS onto ${RootPart} (several GB, give it a few minutes)"
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

# synos.conf only needed the password to get this far: the installed copy
# goes without it
sed '/^UserAccountPassword=/d' "$Conf" > "$Target/etc/syn-os/synos.conf"

# --- Done -----------------------------------------------------------------------
cp -f "$InstallLog" "$Target/var/log/" 2>/dev/null || true
sync
umount -R "$Target"
SynInstallComplete=1

syn_ui::final_banner
syn_ui::info "Take the installation medium out, then: reboot"
syn_ui::info "Log in as ${UserAccountName} and type synos for the desktop."
