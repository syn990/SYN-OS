#!/bin/zsh
# SYN‑OS Volume Strategies (LUKS / LVM)
# /usr/lib/syn-os/syn-volume.zsh

# This script defines the volume management strategies for the SYN-OS installation process. It supports combinations of LUKS encryption and LVM, as well as plain setups without either. 
# The main function `volumeMain` dispatches to the appropriate strategy based on the configuration set in Stage 0. It also handles formatting the ESP for UEFI systems if a separate boot partition is used.
# Goto synos.config.zsh to see how VolumeStrat is set based on user choices during the installation prompts.



set -euo pipefail

# =========================================================
# LUKS + LVM
# =========================================================
volumeStrat_luks_lvm() {
  syn_ui::step "Creating LUKS2 on ${RootPart} (enter a passphrase below)"
  cryptsetup luksFormat \
    --type luks2 \
    --cipher "${LuksCipher}" \
    --key-size "${LuksKeySize}" \
    --pbkdf "${LuksPbkdf}" \
    --batch-mode "${RootPart}"

  LuksUuid="$(cryptsetup luksUUID "${RootPart}")"
  cryptsetup open "${RootPart}" "${LuksLabel}"
  RootMapper="/dev/mapper/${LuksLabel}"
  syn_ui::step_done "LUKS2 volume ready"

  syn_ui::step "Creating LVM on ${RootMapper}"
  pvcreate -ffy "${RootMapper}"
  vgcreate "${VgName}" "${RootMapper}"

  if [ "${SwapSize}" != "0" ]; then
    lvcreate -L "${SwapSize}" -n "${LvSwapName}" "${VgName}"
    SwapDev="/dev/${VgName}/${LvSwapName}"
  fi

  lvcreate -l 100%FREE -n "${LvRootName}" "${VgName}"
  RootFsDev="/dev/${VgName}/${LvRootName}"
  syn_ui::step_done "LVM ready"

  export RootMapper RootFsDev SwapDev LuksUuid
}

# =========================================================
# LUKS only (no LVM)
# =========================================================
volumeStrat_luks_only() {
  syn_ui::step "Creating LUKS2 on ${RootPart} (enter a passphrase below)"
  cryptsetup luksFormat \
    --type luks2 \
    --cipher "${LuksCipher}" \
    --key-size "${LuksKeySize}" \
    --pbkdf "${LuksPbkdf}" \
    --batch-mode "${RootPart}"

  LuksUuid="$(cryptsetup luksUUID "${RootPart}")"
  cryptsetup open "${RootPart}" "${LuksLabel}"
  RootMapper="/dev/mapper/${LuksLabel}"
  RootFsDev="${RootMapper}"
  SwapDev=""
  syn_ui::step_done "LUKS2 volume ready"

  export RootMapper RootFsDev SwapDev LuksUuid
}

# =========================================================
# LVM only (no LUKS)
# =========================================================
volumeStrat_lvm_only() {
  RootMapper="${RootPart}"
  LuksUuid=""

  syn_ui::step "Creating LVM on ${RootPart}"
  pvcreate -ffy "${RootPart}"
  vgcreate "${VgName}" "${RootPart}"

  if [ "${SwapSize}" != "0" ]; then
    lvcreate -L "${SwapSize}" -n "${LvSwapName}" "${VgName}"
    SwapDev="/dev/${VgName}/${LvSwapName}"
  fi

  lvcreate -l 100%FREE -n "${LvRootName}" "${VgName}"
  RootFsDev="/dev/${VgName}/${LvRootName}"
  syn_ui::step_done "LVM ready"

  export RootMapper RootFsDev SwapDev LuksUuid
}

# =========================================================
# Plain (no LUKS, no LVM)
# =========================================================
volumeStrat_plain() {
  RootMapper="${RootPart}"
  RootFsDev="${RootPart}"
  SwapDev=""
  LuksUuid=""

  export RootMapper RootFsDev SwapDev LuksUuid
}

# =========================================================
# Main dispatcher + ESP formatting
# =========================================================
volumeMain() {
  # Format the separate boot partition, if this strategy has one.
  # uefi-bootctl needs a FAT32 ESP; mbr-grub needs a plain filesystem GRUB
  # can read directly (ext4) since it's not an EFI System Partition.
  if [ -n "${BootPart:-}" ] && [ "${BootPart}" != "${RootPart}" ]; then
    [ -b "${BootPart}" ] || { syn_ui::error "BootPart not a block device"; exit 1; }
    case "${PartitionStrat}" in
      uefi-bootctl)
        syn_ui::step "Formatting ESP on ${BootPart}"
        mkfs.vfat -F32 -n ESP "${BootPart}"
        syn_ui::step_done "ESP formatted"
        ;;
      mbr-grub)
        syn_ui::step "Formatting BOOT on ${BootPart}"
        mkfs.ext4 -F -L BOOT "${BootPart}"
        syn_ui::step_done "BOOT formatted"
        ;;
      *)
        syn_ui::error "Don't know how to format boot partition for PartitionStrat '${PartitionStrat}'"
        exit 1
        ;;
    esac
  fi

  case "${VolumeStrat}" in
    luks-lvm)    volumeStrat_luks_lvm ;;
    luks-only)   volumeStrat_luks_only ;;
    lvm-only)    volumeStrat_lvm_only ;;
    plain)       volumeStrat_plain ;;
    *) syn_ui::error "Unknown VolumeStrat '${VolumeStrat}'"; exit 1 ;;
  esac
}
