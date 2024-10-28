#!/bin/bash

# Partition variables
# You will need to modify these to match what devices you expect mkfs, parted and mounting commands to use

WIPE_DISK_990="/dev/vda"
BOOT_PART_990="/dev/vda1"
ROOT_PART_990="/dev/vda2"
BOOT_MOUNT_LOCATION_990="/mnt/boot"
ROOT_MOUNT_LOCATION_990="/mnt/"
BOOT_FILESYSTEM_990="fat32"
ROOT_FILESYSTEM_990="f2fs"


# You can remove these variables, remove the function from SYN-INSTALLER-MAIN.sh
# and instead insert your own partitioning, like the syn-installer0.sh (circa 2019) below:
# 
#    parted --script /dev/sda mklabel gpt mkpart primary fat32 1Mib 200Mib set 1 boot on
#    parted --script /dev/sda mkpart primary ext4 201Mib 100%
#        mkfs.vfat -F /dev/sda1
#        mkfs.ext4 -F /dev/sda2
#            mount /dev/sda2 /mnt
#            mkdir /mnt/boot/
#            mount /dev/sda1 /mnt/boot

# SYN-INSTALLER-MAIN.sh is using parted with the above variables to wipe and format the disks. 
# Alternativley, you can just use cfdisk, gdisk, or any other partioning software or
# hardware, even a Windows installer, so long as the linux kernel can read it and boot from it...

# When going rouge and removing the partitioning you will also have to mount, and make filesystems..
