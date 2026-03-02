#!/bin/zsh
# SYN‑OS Filesystem Strategies
# /usr/lib/syn-os/syn-filesystem.zsh

# This script provides a unified interface for formatting filesystems and swap partitions during the SYN-OS installation process.
# it is used by multiple installation scripts to ensure consistent formatting logic across different filesystem types and swap configurations.
# You may need to modify this script if you want to add support for additional filesystems or customize the formatting options for existing ones.

set -euo pipefail

# =========================================================
# Filesystem formatting
# =========================================================
filesystemMain() {
  [ -b "${RootFsDev}" ] || { echo "RootFsDev not a block device"; exit 1; }

  echo "Formatting root fs (${FilesystemStrat})…"
  case "${FilesystemStrat}" in
    ext4)  mkfs.ext4 -F -L ROOT "${RootFsDev}" ;;
    f2fs)  mkfs.f2fs -f -l ROOT "${RootFsDev}" ;;
    btrfs) mkfs.btrfs -f -L ROOT "${RootFsDev}" ;;
    xfs)   mkfs.xfs -f -L ROOT "${RootFsDev}" ;;
    *) echo "ERROR: Unknown FilesystemStrat '${FilesystemStrat}'"; exit 1 ;;
  esac

  # Format swap if present
  if [ -n "${SwapDev:-}" ]; then
    [ -b "${SwapDev}" ] || { echo "SwapDev not a block device"; exit 1; }
    echo "Formatting swap…"
    mkswap -L SWAP "${SwapDev}"
  fi
}
