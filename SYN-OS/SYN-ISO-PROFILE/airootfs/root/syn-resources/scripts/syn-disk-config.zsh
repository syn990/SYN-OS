# =============================================================================
# SYN-OS Disk Inputs
#
# Edit these values to control the target disk and filesystems.
# This file is sourced by stage0. Do not add logic here.
#
# ⚠️ DESTRUCTIVE: The disk specified in DISK will be wiped.
# =============================================================================

# -----------------------------------------------------------------------------#
# (EDIT THESE HERE FOR YOUR PERSONAL PREFERENCES)
# -----------------------------------------------------------------------------#

# Target block device (WILL BE WIPED)
# Examples:
#   /dev/vda      (virtio)
#   /dev/sda      (SATA/SCSI)
#   /dev/nvme0n1  (NVMe)
DISK="/dev/sda"          # e.g. /dev/sda, /dev/vda, /dev/nvme0n1

# UEFI ESP size
BOOT_SIZE="512MiB"

# Filesystems
BOOT_FS="fat32"          # For UEFI, keep fat32/vfat
ROOT_FS="f2fs"           # ext4 | f2fs | btrfs | xfs

# Mount points
ROOT_MOUNT_LOCATION="/mnt"
BOOT_MOUNT_LOCATION="/mnt/boot"

# Require explicit opt-in before wiping (SHOULD SAY YES BUT THIS IS A DEV BUILD!!!)
REQUIRE_WIPE_CONFIRM="NO"  # YES|NO

export DISK BOOT_SIZE BOOT_FS ROOT_FS \
       ROOT_MOUNT_LOCATION BOOT_MOUNT_LOCATION \
       REQUIRE_WIPE_CONFIRM