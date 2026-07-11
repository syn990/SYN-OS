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

closeHolder() {
  local dev="$1" name="${1:t}" holder vg mapperName

  for holder in /sys/class/block/${name}/holders/*(N); do
    closeHolder "/dev/${holder:t}"
  done

  # lvs exits nonzero (not just empty output) when $dev isn't a PV at all —
  # `|| true` is required, not just the `2>/dev/null` already here, or a
  # plain partition aborts the whole install under set -e.
  vg="$(lvs --noheadings -o vg_name "$dev" 2>/dev/null | tr -d ' ' || true)"
  if [ -n "$vg" ]; then
    vgchange -an "$vg" 2>/dev/null || true
  else
    cryptsetup close "$name" 2>/dev/null || true
  fi

  # A device-mapper target can outlive the LVM/LUKS metadata that created
  # it (e.g. an interrupted install left vg0-root active, then the backing
  # partition got wiped/reused) — lvs and cryptsetup close above both see
  # nothing to act on in that case. dmsetup only accepts the mapper's own
  # name (e.g. "vg0-root"), not the kernel device name ("dm-0") this
  # function is called with, so resolve it first via the device path.
  if [ -z "$vg" ] && command -v dmsetup >/dev/null 2>&1; then
    mapperName="$(dmsetup info -c --noheadings -o name "$dev" 2>/dev/null || true)"
    [ -n "$mapperName" ] && dmsetup remove "$mapperName" 2>/dev/null || true
  fi
}

clearDiskHolders() {
  local disk="$1" part

  swapoff -a 2>/dev/null || true

  for part in "${disk}"p*(N) "${disk}"[0-9]*(N); do
    [ -b "$part" ] || continue
    closeHolder "$part"
  done
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
  clearDiskHolders "${Disk}"

  syn_ui::info "Zeroing first 4 MiB on ${Disk}…"
  dd if=/dev/zero of="${Disk}" bs=1M count=4 status=none || true
  sync

  case "${PartitionStrat}" in
    uefi-bootctl) partitionStrat_uefi_bootctl ;;
    mbr-syslinux) partitionStrat_mbr_syslinux ;;
    mbr-grub)     partitionStrat_mbr_grub ;;
    *) syn_ui::error "Unknown PartitionStrat '${PartitionStrat}'"; exit 1 ;;
  esac

  # The 4MiB zero above only clears the disk's own partition-table header —
  # it can't reach a filesystem/LUKS/LVM signature sitting further in, at the
  # start of whatever partition happens to land there. On a disk reused
  # across test installs (different VolumeStrat/FilesystemStrat each time,
  # same disk), that old signature survives partitioning untouched: mkfs
  # succeeds and reports the new type correctly, but the kernel can still see
  # the old signature underneath and mount/probe gets confused about which
  # one is real. cryptsetup luksFormat and pvcreate -ffy each happen to clear
  # this as a side effect of what they do, but plain mkfs and the boot
  # partition's mkfs never did — so it only ever surfaced on the paths that
  # skip LUKS/LVM. Fixed once, here, for every partition this strategy just
  # created, rather than patched into each volume/filesystem strategy
  # separately.
  for p in "${BootPart:-}" "${RootPart:-}"; do
    [ -n "$p" ] && [ -b "$p" ] && wipefs -a "$p" >/dev/null
  done
}
