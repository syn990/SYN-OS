#!/bin/zsh
# =============================================================================
#                              SYN-OS Disk Config
#
# Purpose:
#   Central place for disk and partition variables used by the installer
#   and ISO build scripts. By editing this file you can change the target
#   storage configuration (e.g. SATA vs NVMe) without modifying multiple scripts.
#
# Advisory:
#   Keep this file limited to **simple variable assignments** only.  
#   Adding commands, conditionals, or runtime logic here can cause staging
#   scripts (stage0/stage1) to misbehave or break.  
#   All logic (detection, chroot-only actions, etc.) belongs in the staging
#   scripts themselves. This ensures sourcing this file is always safe.
#
# Meta:
#   SYN-OS      : The Syntax Operating System
#   Author      : William Hayward-Holland (Syntax990)
#   License     : MIT License
# =============================================================================

# Primary storage device to be wiped. Change to match your system:
#   "/dev/vda"  – Typical for virtual disks
#   "/dev/sda"  – SATA drives
#   "/dev/nvme0n1" – NVMe devices
WIPE_DISK_990="/dev/vda"

# Boot partition (suffix differs between SATA/virtual vs NVMe).
BOOT_PART_990="${WIPE_DISK_990}1"

# Root partition (again, suffix differs with NVMe).
ROOT_PART_990="${WIPE_DISK_990}2"

# Mount points. The installer will create these directories if missing.
BOOT_MOUNT_LOCATION_990="/mnt/boot"
ROOT_MOUNT_LOCATION_990="/mnt"

# Filesystem types. FAT32 is standard for UEFI boot partitions.
# Root can be f2fs, ext4, btrfs, etc.
BOOT_FILESYSTEM_990="fat32"
ROOT_FILESYSTEM_990="f2fs"

# Export so all sourced scripts (stage0/stage1/build) can use them.
export WIPE_DISK_990 BOOT_PART_990 ROOT_PART_990 \
       BOOT_MOUNT_LOCATION_990 ROOT_MOUNT_LOCATION_990 \
       BOOT_FILESYSTEM_990 ROOT_FILESYSTEM_990

# vim: set ft=zsh tw=0 nowrap:
