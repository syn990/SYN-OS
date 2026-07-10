#!/bin/zsh
# SYN‑OS Partition Strategies
# /usr/lib/syn-os/syn-partition.zsh

# This script defines the partitioning strategies for the SYN-OS installation process. It supports both UEFI with systemd-boot (GPT) and legacy BIOS with syslinux (MBR).
# The main function `partitionMain` dispatches to the appropriate strategy based on the configuration set in Stage 0.
# 
set -euo pipefail

# =========================================================
# Helpers
# =========================================================
waitForBlock() {
  local dev="$1"
  local i=0
  while [ $i -lt 50 ]; do
    [ -b "$dev" ] && return 0
    sleep 0.1
    i=$((i+1))
  done
  syn_ui::error "Timeout waiting for block device: $dev"
  return 1
}

# =========================================================
# UEFI + systemd-boot (GPT)
# =========================================================
partitionStrat_uefi_bootctl() {
  syn_ui::step "Creating GPT with ESP (${BootSize}) + ROOT"
  parted -a optimal --script "${Disk}" \
    mklabel gpt \
    mkpart primary 1MiB "${BootSize}" name 1 ESP set 1 esp on \
    mkpart primary "${BootSize}" 100% name 2 ROOT

  partprobe "${Disk}" || true
  udevadm settle || true

  BootPart="${Disk}p1"; [ -b "${BootPart}" ] || BootPart="${Disk}1"
  RootPart="${Disk}p2"; [ -b "${RootPart}" ] || RootPart="${Disk}2"

  waitForBlock "${RootPart}" || { syn_ui::error "ROOT partition not found"; exit 1; }
  waitForBlock "${BootPart}" || { syn_ui::error "BOOT partition not found"; exit 1; }
  syn_ui::step_done "GPT partitions ready"

  export BootPart RootPart
}

# =========================================================
# MBR + syslinux (MSDOS)
# =========================================================
partitionStrat_mbr_syslinux() {
  syn_ui::step "Creating MSDOS (MBR) with single ROOT"
  parted -a optimal --script "${Disk}" mklabel msdos mkpart primary 1MiB 100%

  partprobe "${Disk}" || true
  udevadm settle || true

  RootPart="${Disk}p1"; [ -b "${RootPart}" ] || RootPart="${Disk}1"
  BootPart="${RootPart}"

  waitForBlock "${RootPart}" || { syn_ui::error "ROOT partition not found"; exit 1; }
  syn_ui::step_done "MSDOS partition ready"

  export BootPart RootPart
}

# =========================================================
# MBR + GRUB (MSDOS) — separate unencrypted /boot
# =========================================================
# syslinux has no LUKS support, so an encrypted BIOS/MBR install needs GRUB's
# cryptomount instead — and GRUB needs an unencrypted place to read
# /boot/grub/grub.cfg and the kernel/initramfs from before it can decrypt
# anything. Unlike partitionStrat_mbr_syslinux, this always creates a
# separate small boot partition (${BootSize}), same shape as the UEFI ESP
# layout, so the existing BootPart != RootPart handling in syn-volume.zsh
# and syn-mount.zsh applies here too.
partitionStrat_mbr_grub() {
  syn_ui::step "Creating MSDOS with BOOT (${BootSize}) + ROOT"
  parted -a optimal --script "${Disk}" \
    mklabel msdos \
    mkpart primary ext4 1MiB "${BootSize}" \
    mkpart primary "${BootSize}" 100%

  partprobe "${Disk}" || true
  udevadm settle || true

  BootPart="${Disk}p1"; [ -b "${BootPart}" ] || BootPart="${Disk}1"
  RootPart="${Disk}p2"; [ -b "${RootPart}" ] || RootPart="${Disk}2"

  waitForBlock "${BootPart}" || { syn_ui::error "BOOT partition not found"; exit 1; }
  waitForBlock "${RootPart}" || { syn_ui::error "ROOT partition not found"; exit 1; }
  syn_ui::step_done "MSDOS partitions ready"

  export BootPart RootPart
}

# =========================================================
# Main dispatcher
# =========================================================
partitionMain() {
  syn_ui::info "Zeroing first 4 MiB on ${Disk}…"
  dd if=/dev/zero of="${Disk}" bs=1M count=4 status=none || true
  sync

  case "${PartitionStrat}" in
    uefi-bootctl) partitionStrat_uefi_bootctl ;;
    mbr-syslinux) partitionStrat_mbr_syslinux ;;
    mbr-grub)     partitionStrat_mbr_grub ;;
    *) syn_ui::error "Unknown PartitionStrat '${PartitionStrat}'"; exit 1 ;;
  esac
}
