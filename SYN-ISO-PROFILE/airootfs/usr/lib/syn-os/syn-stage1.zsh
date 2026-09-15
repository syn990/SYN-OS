#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                           S Y N - S T A G E 1
#
#   Runs inside the chroot after Stage 0: configures locale/hostname/time/
#   console, creates the user account, builds the initramfs for the chosen
#   volume strategy (LUKS/LVM), and installs the bootloader. Reads
#   synos.conf (via syn-config.zsh) for static settings and install.state
#   for facts Stage 0 only knows once disk prep has actually run
#   (RootFsDev, SwapDev, LuksUuid, ...).
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-STAGE1 (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

# Load config + UI
source /usr/lib/syn-os/syn-config.zsh
source /usr/lib/syn-os/syn-ui.zsh

# Load derived state from Stage 0
State="/etc/syn-os/install.state"
if [ -r "$State" ]; then
  source "$State"
else
  syn_ui::error "Missing install.state at $State"
  exit 1
fi

syn_ui::step "Stage 1: ${PartitionStrat} + ${VolumeStrat}"
syn_ui::info "ROOT device: ${RootFsDev}"
syn_ui::info "SWAP: ${SwapDev:-none}"
syn_ui::info "LUKS: ${LuksUuid:+yes}${LuksUuid:-no}"

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
syn_ui::step_done "Locale, hostname, time, console configured"

# KeyMap only reaches the console (vconsole.conf/loadkeys above) — labwc
# reads its own layout from XKB_DEFAULT_LAYOUT in ~/.config/labwc/environment
# (labwc-config(5)), which nothing was ever setting. Console keymap names
# and XKB layout codes differ for some layouts (uk -> gb); pass through
# unchanged otherwise.
case "$KeyMap" in
  uk) XkbLayout="gb" ;;
  *)  XkbLayout="$KeyMap" ;;
esac
LabwcEnv="/etc/skel/.config/labwc/environment"
if [ -f "$LabwcEnv" ]; then
  sed -i "/^XKB_DEFAULT_LAYOUT=/d" "$LabwcEnv"
  echo "XKB_DEFAULT_LAYOUT=${XkbLayout}" >> "$LabwcEnv"
  syn_ui::step_done "labwc keyboard layout set to ${XkbLayout} (from KeyMap=${KeyMap})"
fi

# doas + sudo shim setup
if command -v doas >/dev/null 2>&1; then
  echo "permit persist :wheel" > /etc/doas.conf
  chmod 600 /etc/doas.conf
  printf '#!/bin/sh\nexec doas "$@"\n' > /usr/bin/sudo
  chmod 755 /usr/bin/sudo
  pacman -Rdd --noconfirm sudo 2>/dev/null || true
fi

# User account setup
: "${UserAccountName:?UserAccountName not set}"
: "${UserAccountPassword:?UserAccountPassword not set}"
if [ "$UserAccountPassword" = "CHANGE_ME" ]; then
  syn_ui::error "UserAccountPassword is still 'CHANGE_ME' in synos.conf — set a real password before installing."
  exit 1
fi
if ! id -u "$UserAccountName" >/dev/null 2>&1; then
  useradd -m -G wheel -s "$UserShell" "$UserAccountName"
fi
echo "${UserAccountName}:${UserAccountPassword}" | chpasswd
syn_ui::step_done "Password set for ${UserAccountName}"

# Renders this install its own take on every theme's wallpaper, replacing
# the stock PNGs skel just copied in — same palette per theme, slightly
# different composition (light position, ring/band counts, ...) each
# install, since syn-wallgen reseeds its RNG from /dev/urandom every run.
# syn-wallgen is a prebuilt binary (see the native-tools note above), so
# there's no compile step here — just running it. UserHome comes from
# getent rather than assuming /home/$name, since useradd -m honors a
# non-default HOME dir if synos.conf ever sets one.
UserHome="$(getent passwd "$UserAccountName" | cut -d: -f6)"
if [ -x /usr/lib/syn-os/syn-wallgen ] && [ -d "$UserHome/.config/syn-os/themes" ]; then
  syn_ui::step "Generating this install's wallpaper set"
  if /usr/lib/syn-os/syn-wallgen --themes-dir "$UserHome/.config/syn-os/themes" --out-dir "$UserHome/.wallpaper"; then
    chown -R "${UserAccountName}:${UserAccountName}" "$UserHome/.wallpaper"
    syn_ui::step_done "Wallpapers generated for ${UserAccountName}"
  else
    syn_ui::error "syn-wallgen failed — stock wallpapers from the dotfile overlay are still in place, continuing"
  fi
fi

# synos.conf only needed the password to reach this point — strip it
# rather than leave it in plaintext on the installed disk.
sed -i '/^UserAccountPassword=/d' "$SYNOS_CONF"

# Every locally-authored native tool (syn-filemanager, the waybar module
# backends, syn-crypter, syn-connect, syn-wallgen) is already on this disk
# by this point — syn-pacstrap.zsh copies each one's already-built binary
# straight from the live ISO before Stage 0 even chroots in here. Nothing
# to build.

# mkinitcpio hooks — same set for UEFI and BIOS
configure_mkinitcpio() {
  HOOKS=(base udev autodetect modconf kms keyboard keymap consolefont block)
  if [ "${Encryption:-no}" = "yes" ]; then
    HOOKS+=(encrypt)
  fi
  if [ "${UseLvm:-no}" = "yes" ]; then
    HOOKS+=(lvm2)
  fi
  HOOKS+=(filesystems fsck)

  syn_ui::info "Configuring mkinitcpio with HOOKS: ${HOOKS[*]}"
  sed -i "s/^HOOKS=.*/HOOKS=(${HOOKS[*]})/" /etc/mkinitcpio.conf
}

# Rebuilds with the hooks configure_mkinitcpio just set — critical for
# LUKS, where the initramfs needs the encrypt hook to prompt for a
# passphrase at boot.
syn_ui::step "Building initramfs"
configure_mkinitcpio
mkinitcpio -P
syn_ui::step_done "Initramfs built"

# Bootloader configuration
RootCmdline=""
if [ "${Encryption:-no}" = "yes" ]; then
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

syn_ui::step "Installing bootloader (${PartitionStrat})"
case "${PartitionStrat}" in
uefi-bootctl|uefi-refind)
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

  # uefi-refind chains to the systemd-boot entry just created above —
  # rEFInd becomes what firmware actually boots first. This is the piece
  # that matters for real Apple hardware: Mac firmware doesn't reliably
  # persist a plain systemd-boot NVRAM entry across reboots, and rEFInd's
  # own installer handles registering itself (including the ESP fallback
  # path, EFI/BOOT/BOOTX64.EFI) in a way that does.
  if [ "$PartitionStrat" = "uefi-refind" ]; then
    refind-install
  fi
  ;;
mbr-syslinux)
  syslinux-install_update -i -a -m || true
  if [ -f /boot/syslinux/syslinux.cfg ]; then
    sed -i "s|APPEND .*|APPEND ${RootCmdline} ${ResumeOpt} vconsole.keymap=${KeyMap} ${KernelOpts}|" /boot/syslinux/syslinux.cfg
    # Branded graphical menu, same asset/mechanism as the live ISO's own
    # syslinux.cfg (SYN-ISO-PROFILE/syslinux/) — syn-pacstrap.zsh staged
    # the source PNG to /usr/share/syn-os/branding, copy it into place
    # here now that syslinux-install_update has created /boot/syslinux/.
    # syslinux-install_update's auto-generated config has no UI directive
    # at all by default (vesamenu is opt-in), so this replaces the line if
    # present and prepends it otherwise, rather than assuming a sed
    # replace has something to match.
    if [ -f /usr/share/syn-os/branding/syslinux-splash.png ]; then
      install -Dm755 /usr/share/syn-os/branding/syslinux-splash.png /boot/syslinux/splash.png
      if grep -q "^UI " /boot/syslinux/syslinux.cfg; then
        sed -i "s|^UI .*|UI vesamenu.c32|" /boot/syslinux/syslinux.cfg
      else
        sed -i "1i UI vesamenu.c32" /boot/syslinux/syslinux.cfg
      fi
      if ! grep -q "^MENU BACKGROUND" /boot/syslinux/syslinux.cfg; then
        sed -i "/^UI vesamenu.c32/a MENU BACKGROUND splash.png" /boot/syslinux/syslinux.cfg
      fi
    fi
  fi
  ;;
# See syn-disk.zsh's partitionStrat_mbr_grub for why this strategy
# needs its own unencrypted /boot. Because of that, GRUB itself never
# touches encryption — cryptdevice= is resolved by the initramfs's
# encrypt hook at boot, same as uefi-bootctl.
mbr-grub|mbr-grub-btrfs|mbr-grub-xfs)
  # GRUB module name matches PartitionStrat 1:1 except mbr-grub's ext4,
  # which is read by the "ext2" module — GRUB's ext2 driver auto-detects
  # ext2/3/4, there's no separate ext4 module in mainline GRUB (confirmed
  # against the installed grub package: /usr/lib/grub/i386-pc/ has
  # exactly ext2.mod, btrfs.mod, xfs.mod — one module per family). This
  # must stay in lockstep with syn-disk.zsh's volumeMain mkfs call (same
  # PartitionStrat value) — a mismatch means the disk formats fine but
  # GRUB can't read its own /boot at boot time: install succeeds, machine
  # doesn't boot.
  case "${PartitionStrat}" in
    mbr-grub)       grubFsModule="ext2"  ;;
    mbr-grub-btrfs) grubFsModule="btrfs" ;;
    mbr-grub-xfs)   grubFsModule="xfs"   ;;
  esac

  grub-install --target=i386-pc --recheck --boot-directory=/boot \
    --modules="part_msdos ${grubFsModule} biosdisk" "${Disk}"

  INITRD_LINES="initrd /initramfs-linux.img"
  [ -f /boot/intel-ucode.img ] && INITRD_LINES="initrd /intel-ucode.img /initramfs-linux.img"
  [ -f /boot/amd-ucode.img ] && INITRD_LINES="initrd /amd-ucode.img /initramfs-linux.img"

  mkdir -p /boot/grub
  # Same branded asset as the live ISO's own grub/splash.png —
  # syn-pacstrap.zsh staged it to /usr/share/syn-os/branding, copy it
  # into place now that /boot/grub exists. Degrades gracefully to a plain
  # text menu if it's ever missing.
  if [ -f /usr/share/syn-os/branding/grub-splash.png ]; then
    install -Dm755 /usr/share/syn-os/branding/grub-splash.png /boot/grub/splash.png
  fi
  {
    echo "set default=0"
    echo "set timeout=0"
    if [ -f /boot/grub/splash.png ]; then
      echo "insmod png"
      echo "if background_image /boot/grub/splash.png; then"
      echo "  set color_normal=white/black"
      echo "fi"
    fi
    echo ""
    echo "menuentry 'SYN-OS' {"
    echo "  insmod part_msdos"
    echo "  insmod ${grubFsModule}"
    echo "  linux /vmlinuz-linux ${RootCmdline} ${ResumeOpt} vconsole.keymap=${KeyMap} ${KernelOpts}"
    echo "  ${INITRD_LINES}"
    echo "}"
  } > /boot/grub/grub.cfg
  ;;
uefi-clover)
  syn_ui::error "PartitionStrat=uefi-clover is reserved for future support — not yet implemented. Use uefi-bootctl or uefi-refind instead."
  exit 1
  ;;
*)
  syn_ui::error "Unsupported PartitionStrat '$PartitionStrat'"
  exit 1
  ;;
esac
syn_ui::step_done "Bootloader installed"

# Enable baseline services
systemctl enable dhcpcd.service 2>/dev/null || true
systemctl enable iwd.service    2>/dev/null || true
systemctl enable linux-modules-cleanup.service 2>/dev/null || true  # kernel-modules-hook: sweep stale module trees at boot

if [ "${EnableSsh:-no}" = "yes" ]; then
  systemctl enable sshd.service 2>/dev/null || true
  syn_ui::step_done "sshd enabled (EnableSsh=yes in synos.conf)"
fi

# DS4/DualSense pairs fine but bonding never fully completes, so
# BlueZ's default ClassicBondedOnly=true silently drops the HID
# connection — light bar goes solid, no input ever arrives.
sed -i 's/^#\?ClassicBondedOnly=.*/ClassicBondedOnly=false/' /etc/bluetooth/input.conf
syn_ui::step_done "Bluetooth HID fixed for DS4/DualSense controllers"

# zstd-compressed RAM-backed swap. The modules-load.d entry matters: without
# it the zram module isn't guaranteed to be loaded before zram-generator's
# systemd units run, which leaves them waiting forever on a zram0 device
# that never appears.
if [ "${ZramPercent:-0}" != "0" ]; then
  cat > /etc/systemd/zram-generator.conf <<EOF
[zram0]
zram-size = min(ram * ${ZramPercent} / 100, ${ZramMaxMiB})
compression-algorithm = zstd
EOF
  echo zram > /etc/modules-load.d/zram.conf
  syn_ui::step_done "zram swap configured (${ZramPercent}% of RAM, capped at ${ZramMaxMiB}MiB)"
fi

# Only enable qemu-guest-agent when actually running under QEMU/KVM — on
# real hardware it would just idle forever with no virtio-serial channel
# to talk to, which is exactly the unnecessary-boot-noise pattern this
# project avoids elsewhere (see Philosophy).
if [ "$(systemd-detect-virt 2>/dev/null)" = "kvm" ] || [ "$(systemd-detect-virt 2>/dev/null)" = "qemu" ]; then
  systemctl enable qemu-guest-agent.service 2>/dev/null || true
  syn_ui::step_done "qemu-guest-agent enabled (running under QEMU/KVM)"
fi

# Finalize installation and prompt for reboot
syn_ui::final_banner
