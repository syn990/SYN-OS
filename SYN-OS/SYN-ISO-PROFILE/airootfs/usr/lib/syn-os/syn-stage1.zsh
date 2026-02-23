#!/bin/zsh
# SYN-OS Stage 1: configure system, bootloader, users
# /usr/lib/syn-os/syn-stage1.zsh

set -euo pipefail

# Load config + UI
source /usr/lib/syn-os/syn-config.zsh
source /usr/lib/syn-os/ui.zsh

# Load derived state from Stage 0
State="/etc/syn-os/install.state"
if [ -r "$State" ]; then
  source "$State"
else
  echo "ERROR: Missing install.state at $State"
  exit 1
fi

echo "SYN-OS Stage 1 — ${PartitionStrat} + ${VolumeStrat}"
echo "ROOT device: ${RootFsDev}"
echo "SWAP: ${SwapDev:-none}"
echo "LUKS: ${LuksUuid:+yes}${LuksUuid:-no}"

# Locale / Hostname / Time / Console
echo "$LocaleGen" > /etc/locale.gen
locale-gen
echo "LANG=$Locale" > /etc/locale.conf
echo "$Hostname" > /etc/hostname

if [ -e "/usr/share/zoneinfo/$TimeZone" ]; then
  ln -sf "/usr/share/zoneinfo/$TimeZone" /etc/localtime
else
  ln -sf "/usr/share/zoneinfo/Europe/London" /etc/localtime
fi

printf "KEYMAP=%s\nFONT=%s\n" "$KeyMap" "$VconsoleFont" > /etc/vconsole.conf
hwclock --systohc

# doas + sudo shim
if command -v doas >/dev/null 2>&1; then
  echo "permit persist :wheel" > /etc/doas.conf
  chmod 600 /etc/doas.conf
  printf '#!/bin/sh\nexec doas "$@"\n' > /usr/bin/sudo
  chmod 755 /usr/bin/sudo
  pacman -Rdd --noconfirm sudo 2>/dev/null || true
fi

# User
: "${UserAccountName:?UserAccountName not set}"
if ! id -u "$UserAccountName" >/dev/null 2>&1; then
  useradd -m -G wheel -s "$UserShell" "$UserAccountName"
fi
echo "Set password for $UserAccountName:"
passwd "$UserAccountName" </dev/tty

# System overlays deployed during pacstrap

# mkinitcpio: Use traditional hooks for both UEFI and BIOS (more reliable)
configure_mkinitcpio() {
  HOOKS=(base udev autodetect modconf kms keyboard keymap consolefont block)
  if [[ "$VolumeStrat" == luks* ]]; then
    HOOKS+=(encrypt)
  fi
  if [[ "$VolumeStrat" == *lvm* ]]; then
    HOOKS+=(lvm2)
  fi
  HOOKS+=(filesystems fsck)

  echo "Configuring mkinitcpio with HOOKS: ${HOOKS[*]}"
  sed -i "s/^HOOKS=.*/HOOKS=(${HOOKS[*]})/" /etc/mkinitcpio.conf
}

configure_mkinitcpio
mkinitcpio -P

# Bootloader configuration
RootCmdline=""
if [[ "$VolumeStrat" == luks* ]]; then
  RootCmdline="cryptdevice=UUID=${LuksUuid}:${LuksLabel} root=${RootFsDev} rw"
else
  if RootUuidPrint="$(blkid -s UUID -o value "$RootFsDev" 2>/dev/null || true)"; then
    RootCmdline="root=UUID=${RootUuidPrint} rw"
  else
    RootCmdline="root=${RootFsDev} rw"
  fi
fi

ResumeOpt=""
if [ -n "${SwapDev:-}" ]; then
  if SwapUuidPrint="$(blkid -s UUID -o value "$SwapDev" 2>/dev/null || true)"; then
    ResumeOpt="resume=UUID=${SwapUuidPrint}"
  fi
fi

if [ "$PartitionStrat" = "uefi-bootctl" ]; then
  bootctl --path=/boot install
  mkdir -p /boot/loader/entries
  cat > /boot/loader/loader.conf <<'EOF'
default syn.conf
timeout 0
editor  0
EOF

  INITRD_LINES="initrd  /initramfs-linux.img"
  [ -f /boot/intel-ucode.img ] && INITRD_LINES="initrd  /intel-ucode.img\n$INITRD_LINES"
  [ -f /boot/amd-ucode.img ] && INITRD_LINES="initrd  /amd-ucode.img\n$INITRD_LINES"

  cat > /boot/loader/entries/syn.conf <<EOF
title   SYN-OS
linux   /vmlinuz-linux
${INITRD_LINES}
options ${RootCmdline} ${ResumeOpt} vconsole.keymap=${KeyMap} ${KernelOpts}
EOF
elif [ "$PartitionStrat" = "mbr-syslinux" ]; then
  syslinux-install_update -i -a -m || true
  if [ -f /boot/syslinux/syslinux.cfg ]; then
    sed -i "s|APPEND .*|APPEND ${RootCmdline} ${ResumeOpt} vconsole.keymap=${KeyMap} ${KernelOpts}|" /boot/syslinux/syslinux.cfg
  fi
else
  echo "ERROR: Unsupported PartitionStrat '$PartitionStrat'"
  exit 1
fi

# Enable baseline services
systemctl enable dhcpcd.service 2>/dev/null || true
systemctl enable iwd.service    2>/dev/null || true

syn_ui::final_banner
