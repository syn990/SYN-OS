#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - F I L E S Y S T E M
#
#   Formats the root filesystem and swap partition during installation.
#   Adding a new filesystem type also needs its mkfs package added to
#   syn-packages.zsh.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-FILESYSTEM (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail
filesystemMain() {
  [ -b "${RootFsDev}" ] || { syn_ui::error "RootFsDev not a block device"; exit 1; }

  # mkfs.* only needs the userspace tool, but mount's auto-detect (see
  # mountMain) needs the kernel module already loaded — load it explicitly
  # since it isn't guaranteed to autoload on this live image.
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
