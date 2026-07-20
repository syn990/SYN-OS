#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                           S Y N - D I S K
#
#   Disk-prep pipeline for Stage 0: partitioning, volume management
#   (LUKS/LVM), filesystem creation, and mounting, in that order.
#   partitionMain -> volumeMain -> filesystemMain -> mountMain. Combined
#   into one file because each stage only ever runs once, in this fixed
#   order, from syn-stage0.zsh — splitting them never bought independent
#   reuse, just an undeclared dependency on syn-ui.zsh being sourced first.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-DISK (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

# =========================================================
# Partitioning: shared helpers
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
# Partitioning: UEFI + systemd-boot (GPT)
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
# Partitioning: MBR + syslinux (MSDOS)
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
# Partitioning: MBR + GRUB (MSDOS) — separate unencrypted /boot
# =========================================================
# GRUB needs to read grub.cfg and the kernel/initramfs from an unencrypted
# partition before it can decrypt anything, so — unlike mbr_syslinux — this
# always creates a separate boot partition, the same shape as the UEFI ESP
# layout that volumeMain/mountMain below already handle.
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
# Partitioning: main dispatcher
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

  # The 4MiB zero above only clears the partition-table header, not a
  # filesystem/LUKS/LVM signature further into a reused partition — wipe
  # every partition explicitly so mount/probe can't see a stale signature
  # underneath the new one.
  for p in "${BootPart:-}" "${RootPart:-}"; do
    [ -n "$p" ] && [ -b "$p" ] && wipefs -a "$p" >/dev/null
  done
}

# =========================================================
# Volume: LUKS + LVM
# =========================================================
volumeStrat_luks_lvm() {
  syn_ui::step "Creating LUKS2 on ${RootPart} (LuksPassphrase from synos.conf)"
  printf '%s' "${LuksPassphrase}" | cryptsetup luksFormat \
    --type luks2 \
    --cipher "${LuksCipher}" \
    --key-size "${LuksKeySize}" \
    --pbkdf "${LuksPbkdf}" \
    --batch-mode --key-file=- "${RootPart}"

  LuksUuid="$(cryptsetup luksUUID "${RootPart}")"
  printf '%s' "${LuksPassphrase}" | cryptsetup open --key-file=- "${RootPart}" "${LuksLabel}"
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
# Volume: LUKS only (no LVM)
# =========================================================
volumeStrat_luks_only() {
  syn_ui::step "Creating LUKS2 on ${RootPart} (LuksPassphrase from synos.conf)"
  printf '%s' "${LuksPassphrase}" | cryptsetup luksFormat \
    --type luks2 \
    --cipher "${LuksCipher}" \
    --key-size "${LuksKeySize}" \
    --pbkdf "${LuksPbkdf}" \
    --batch-mode --key-file=- "${RootPart}"

  LuksUuid="$(cryptsetup luksUUID "${RootPart}")"
  printf '%s' "${LuksPassphrase}" | cryptsetup open --key-file=- "${RootPart}" "${LuksLabel}"
  RootMapper="/dev/mapper/${LuksLabel}"
  RootFsDev="${RootMapper}"
  SwapDev=""
  syn_ui::step_done "LUKS2 volume ready"

  export RootMapper RootFsDev SwapDev LuksUuid
}

# =========================================================
# Volume: LVM only (no LUKS)
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
# Volume: plain (no LUKS, no LVM)
# =========================================================
volumeStrat_plain() {
  # partitionMain already wipefs'd every partition this strategy created
  # before volumeMain ever runs, so there's nothing left to clear here —
  # this strategy just needs to point the downstream variables at the raw
  # partition, unlike LUKS/LVM which build a mapper device on top of it.
  RootMapper="${RootPart}"
  RootFsDev="${RootPart}"
  SwapDev=""
  LuksUuid=""

  export RootMapper RootFsDev SwapDev LuksUuid
}

# =========================================================
# Volume: main dispatcher + ESP formatting
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

# =========================================================
# Filesystem: format root + swap
# =========================================================
# Adding a new filesystem type also needs its mkfs package added to
# syn-packages.zsh.
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

# =========================================================
# Mount: root, boot, swap
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
