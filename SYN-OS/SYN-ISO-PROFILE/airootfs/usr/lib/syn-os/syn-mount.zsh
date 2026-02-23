#!/bin/zsh
# SYN‑OS Mount Orchestration
# /usr/lib/syn-os/syn-mount.zsh

set -euo pipefail

# =========================================================
# Mount orchestration
# =========================================================
mountMain() {
  [ -b "${RootFsDev}" ] || { echo "RootFsDev not a block device"; exit 1; }

  echo "Mounting root at ${RootMountLocation}…"
  mkdir -p "${RootMountLocation}"
  mount "${RootFsDev}" "${RootMountLocation}"

  # Mount boot if UEFI (different partition)
  if [ -n "${BootPart:-}" ] && [ "${BootPart}" != "${RootPart}" ]; then
    [ -b "${BootPart}" ] || { echo "BootPart not a block device"; exit 1; }
    echo "Mounting boot at ${BootMountLocation}…"
    mkdir -p "${BootMountLocation}"
    mount "${BootPart}" "${BootMountLocation}"
  fi

  # Enable swap if present
  if [ -n "${SwapDev:-}" ]; then
    [ -b "${SwapDev}" ] && swapon "${SwapDev}" || true
  fi
}
