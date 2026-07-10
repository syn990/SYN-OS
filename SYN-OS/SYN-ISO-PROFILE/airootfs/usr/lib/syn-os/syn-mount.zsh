#!/bin/zsh
# SYN‑OS Mount Orchestration
# /usr/lib/syn-os/syn-mount.zsh

set -euo pipefail

# =========================================================
# Mount orchestration
# =========================================================
mountMain() {
  [ -b "${RootFsDev}" ] || { syn_ui::error "RootFsDev not a block device"; exit 1; }

  syn_ui::step "Mounting filesystems"
  mkdir -p "${RootMountLocation}"
  mount "${RootFsDev}" "${RootMountLocation}"
  syn_ui::info "Root mounted at ${RootMountLocation}"

  # Mount boot if UEFI (different partition)
  if [ -n "${BootPart:-}" ] && [ "${BootPart}" != "${RootPart}" ]; then
    [ -b "${BootPart}" ] || { syn_ui::error "BootPart not a block device"; exit 1; }
    mkdir -p "${BootMountLocation}"
    mount "${BootPart}" "${BootMountLocation}"
    syn_ui::info "Boot mounted at ${BootMountLocation}"
  fi

  # Enable swap if present
  if [ -n "${SwapDev:-}" ]; then
    [ -b "${SwapDev}" ] && swapon "${SwapDev}" || true
  fi
  syn_ui::step_done "Filesystems mounted"
}
