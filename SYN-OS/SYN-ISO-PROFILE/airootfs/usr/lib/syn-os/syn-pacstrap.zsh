#!/bin/zsh
# SYN‑OS Base Install & State Handoff
# /usr/lib/syn-os/syn-pacstrap.zsh

# This script handles the base installation of the Arch Linux system onto the target root partition using `pacstrap`. It also generates the fstab and saves the installation state to a file that will be used by Stage 1 after chrooting.
# Additionally, it deploys a dotfile overlay to the target system to customize the environment for new user accounts, and ensures that the necessary scripts and configuration are copied over for Stage 1 to access.

set -euo pipefail

# =========================================================
# Base install + state persistence
# =========================================================
pacstrapMain() {
  source /usr/lib/syn-os/syn-packages.zsh

  syn_ui::pacman_snack

  # Mirror + keyring
  syn_ui::step "Refreshing mirrors and pacman keyring"
  reflector -c GB -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist || true
  pacman-key --init
  pacman-key --populate archlinux
  pacman -Sy
  syn_ui::step_done "Mirrors and keyring ready"

  # Select bootloader packages
  local -a bootPkgs
  case "${BootloaderStrat}" in
    auto)
      case "${PartitionStrat}" in
        uefi-bootctl) bootPkgs=(efibootmgr systemd) ;;
        mbr-grub)     bootPkgs=(grub) ;;
        *)            bootPkgs=(syslinux) ;;
      esac
      ;;
    systemd-boot) bootPkgs=(efibootmgr systemd) ;;
    syslinux)     bootPkgs=(syslinux) ;;
    grub)         bootPkgs=(grub) ;;
    *)            bootPkgs=() ;;
  esac

  SYNSTALL+=("${bootPkgs[@]}")
# Use syn-packages.zsh arrays to determine the final package list to install with pacstrap
# Pacstrap those packages to the location defined in synos.conf
  syn_ui::step "Installing packages to ${RootMountLocation}"
  pacstrap -K "${RootMountLocation}" "${SYNSTALL[@]}"
  genfstab -U "${RootMountLocation}" >> "${RootMountLocation}/etc/fstab"
  syn_ui::step_done "Base packages installed"

  # Copy current scripts and config to target system for Stage 1 handoff
  install -Dm644 /etc/syn-os/synos.conf "${RootMountLocation}/etc/syn-os/synos.conf"
  for script in /usr/lib/syn-os/*.zsh; do
    install -Dm755 "$script" "${RootMountLocation}/usr/lib/syn-os/$(basename "$script")"
  done

# Deploy dotfile overlay to target system to customize environment for new user accounts
if [ -d /usr/lib/syn-os/DotfileOverlay ]; then
  syn_ui::info "Deploying dotfile overlay to ${RootMountLocation}…"
  cp -r /usr/lib/syn-os/DotfileOverlay/* "${RootMountLocation}/"

  # Make all the good stuff executable
  chmod -R +x "${RootMountLocation}/usr/lib/syn-os"
  chmod -R +x "${RootMountLocation}/etc/skel/.config/labwc"
  chmod -R +x "${RootMountLocation}/etc/skel/.config/waybar"
  chmod -R +x "${RootMountLocation}/etc/skel/.config/superfile"
fi

  # Persist state for Stage 1
  local State="${RootMountLocation}/etc/syn-os/install.state"
  mkdir -p "$(dirname "$State")"
  cat > "$State" <<EOF
PartitionStrat="${PartitionStrat}"
VolumeStrat="${VolumeStrat}"
Encryption="${Encryption}"
UseLvm="${UseLvm}"
FilesystemStrat="${FilesystemStrat}"
BootloaderStrat="${BootloaderStrat}"

Disk="${Disk}"
BootPart="${BootPart:-}"
RootPart="${RootPart:-}"
RootMapper="${RootMapper:-}"
RootFsDev="${RootFsDev}"
SwapDev="${SwapDev:-}"

LuksLabel="${LuksLabel:-cryptroot}"
LuksUuid="${LuksUuid:-}"
VgName="${VgName}"

Hostname="${Hostname}"
UserAccountName="${UserAccountName}"
UserShell="${UserShell}"
Locale="${Locale}"
LocaleGen="${LocaleGen}"
KeyMap="${KeyMap}"
TimeZone="${TimeZone}"
VconsoleFont="${VconsoleFont}"

BootFs="${BootFs}"
RootFs="${RootFs}"
BootMountLocation="${BootMountLocation}"
RootMountLocation="${RootMountLocation}"
KernelOpts="${KernelOpts}"

EOF

  syn_ui::step_done "Base install complete, state saved for Stage 1"
}
