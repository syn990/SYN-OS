#!/bin/zsh
# SYN‑OS Partition Strategies
# /usr/lib/syn-os/syn-partition.zsh

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
  echo "Timeout waiting for block device: $dev" >&2
  return 1
}

# =========================================================
# UEFI + systemd-boot (GPT)
# =========================================================
partitionStrat_uefi_bootctl() {
  echo "Creating GPT with ESP (${BootSize}) + ROOT…"
  parted -a optimal --script "${Disk}" \
    mklabel gpt \
    mkpart primary 1MiB "${BootSize}" name 1 ESP set 1 esp on \
    mkpart primary "${BootSize}" 100% name 2 ROOT

  partprobe "${Disk}" || true
  udevadm settle || true

  BootPart="${Disk}p1"; [ -b "${BootPart}" ] || BootPart="${Disk}1"
  RootPart="${Disk}p2"; [ -b "${RootPart}" ] || RootPart="${Disk}2"

  waitForBlock "${RootPart}" || { echo "ROOT partition not found"; exit 1; }
  waitForBlock "${BootPart}" || { echo "BOOT partition not found"; exit 1; }

  export BootPart RootPart
}

# =========================================================
# MBR + syslinux (MSDOS)
# =========================================================
partitionStrat_mbr_syslinux() {
  echo "Creating MSDOS (MBR) with single ROOT…"
  parted -a optimal --script "${Disk}" mklabel msdos mkpart primary 1MiB 100%

  partprobe "${Disk}" || true
  udevadm settle || true

  RootPart="${Disk}p1"; [ -b "${RootPart}" ] || RootPart="${Disk}1"
  BootPart="${RootPart}"

  waitForBlock "${RootPart}" || { echo "ROOT partition not found"; exit 1; }

  export BootPart RootPart
}

# =========================================================
# Main dispatcher
# =========================================================
partitionMain() {
  echo "Zeroing first 4 MiB on ${Disk}…"
  dd if=/dev/zero of="${Disk}" bs=1M count=4 status=none || true
  sync

  case "${PartitionStrat}" in
    uefi-bootctl) partitionStrat_uefi_bootctl ;;
    mbr-syslinux) partitionStrat_mbr_syslinux ;;
    *) echo "ERROR: Unknown PartitionStrat '${PartitionStrat}'"; exit 1 ;;
  esac
}
