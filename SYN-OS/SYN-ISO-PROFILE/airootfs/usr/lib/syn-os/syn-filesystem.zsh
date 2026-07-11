#!/bin/zsh
# SYN‑OS Filesystem Strategies
# /usr/lib/syn-os/syn-filesystem.zsh

# This script provides a unified interface for formatting filesystems and swap partitions during the SYN-OS installation process.
# it is used by multiple installation scripts to ensure consistent formatting logic across different filesystem types and swap configurations.
# You may need to modify this script if you want to add support for additional filesystems or customize the formatting options for existing ones.
# You are then expected to add the correct packages to packagesx86_64

set -euo pipefail

# =========================================================
# Filesystem formatting
# =========================================================
filesystemMain() {
  [ -b "${RootFsDev}" ] || { syn_ui::error "RootFsDev not a block device"; exit 1; }

  # mkfs.* only needs the userspace tool, so formatting succeeds even if the
  # matching kernel module isn't loaded yet — but mount's auto-detect (no -t
  # given, see mountMain) can only match filesystem types the kernel already
  # knows about. Confirmed on this live image: mkfs.f2fs succeeds, blkid
  # correctly reports TYPE="f2fs" afterwards, but the later mount call still
  # fails because f2fs was never in the running kernel's fs list to begin
  # with (not autoloaded, and mount's own auto-modprobe apparently didn't
  # trigger here either). Re-running the identical mount command after a
  # manual `modprobe f2fs` succeeds immediately against the same partition —
  # loading the module explicitly here closes that gap.
  modprobe "${FilesystemStrat}" 2>/dev/null || true

  syn_ui::step "Formatting root fs (${FilesystemStrat})"
  case "${FilesystemStrat}" in
    ext4)  mkfs.ext4 -F -L ROOT "${RootFsDev}" ;;
    f2fs)  mkfs.f2fs -f -l ROOT "${RootFsDev}" ;;
    btrfs) mkfs.btrfs -f -L ROOT "${RootFsDev}" ;;
    xfs)   mkfs.xfs -f -L ROOT "${RootFsDev}" ;;
    *) syn_ui::error "Unknown FilesystemStrat '${FilesystemStrat}'"; exit 1 ;;
  esac
  syn_ui::step_done "Root fs ready"

  # Format swap if present
  if [ -n "${SwapDev:-}" ]; then
    [ -b "${SwapDev}" ] || { syn_ui::error "SwapDev not a block device"; exit 1; }
    syn_ui::step "Formatting swap"
    mkswap -L SWAP "${SwapDev}"
    syn_ui::step_done "Swap ready"
  fi
}
