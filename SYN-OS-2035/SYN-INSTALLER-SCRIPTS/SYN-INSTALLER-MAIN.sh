#!/bin/bash

# SYN-INSTALLER-MAIN.SH
# This is the main script to be executed when you begin the installation of SYN-OS.
# Variables and other resources are deposited across the ISO. It's all a mess.
# All variables, functions and processes will branch from this script.

# Installation:
# This script aims to do the following: (/root/SYN-INSTALLER-SCRIPTS/*)
# - Map defined partition, file-system and mounting information seen in syn-disk-variables.sh
# - Pacstrap using packages described in syn-pacstrap-variables.sh

# Load additional sources
source /root/SYN-INSTALLER-SCRIPTS/syn-installer-functions.sh
source /root/SYN-INSTALLER-SCRIPTS/syn-disk-variables.sh
source /root/SYN-INSTALLER-SCRIPTS/syn-pacstrap-variables.sh
source /root/SYN-INSTALLER-SCRIPTS/syn-ascii-art.sh

syn_directory_structure_extensive_description
clear
display_syn_os_logo

# Check network connectivity to Arch Linux repositories
ping -c 1 archlinux.org >/dev/null 2>&1

if [ $? -ne 0 ]; then
    printf "\e[1;31mError: Unable to reach Arch Linux repositories. Please check your network connection and try again.\e[0m\n"
    exit 1
fi

# Set install environment to be ready for pacstrap
loadkeys uk                               # Setup the keyboard layout
check_success "Failed to setup keyboard layout"

timedatectl set-ntp true                  # Setup NTP so the time is up-to-date
check_success "Failed to set NTP"

check_if_arch_repo_are_accessible

# Going past this point will wipe the disks based on the variables at the top of this script.
confirm_wipe "Autonomous judgment waiver is now in effect..."
clear
display_system_wipe_warning



        # This all needs to be removed from the main script...


    
    # Announce and declare variables
    printf "\033[1;33m%s\033[0m\n" "WIPE_DISK_990 value: $WIPE_DISK_990"
    sleep 0.2
    printf "\033[1;33m%s\033[0m\n" "BOOT_FILESYSTEM_990 value: $BOOT_FILESYSTEM_990"
    sleep 0.2
    printf "\033[1;33m%s\033[0m\n" "ROOT_FILESYSTEM_990 value: $ROOT_FILESYSTEM_990"
    sleep 0.2
    printf "\033[1;33m%s\033[0m\n" "BOOT_PART_990 value: $BOOT_PART_990"
    sleep 0.2
    printf "\033[1;33m%s\033[0m\n" "ROOT_PART_990 value: $ROOT_PART_990"
    sleep 0.2
    printf "\033[1;33m%s\033[0m\n" "ROOT_MOUNT_LOCATION_990 value: $ROOT_MOUNT_LOCATION_990"
    sleep 0.2
    printf "\033[1;33m%s\033[0m\n" "BOOT_MOUNT_LOCATION_990 value: $BOOT_MOUNT_LOCATION_990"
    sleep 0.2 


        parted --script $WIPE_DISK_990 mklabel gpt mkpart primary $BOOT_FILESYSTEM_990 1MiB 200MiB set 1 boot on
        parted --script $WIPE_DISK_990 mkpart primary $ROOT_FILESYSTEM_990 201MiB 100%
        
        cryptsetup -y -v luksFormat /dev/vda2
        cryptsetup open /dev/vda2 cryptroot
        mkfs.ext4 /dev/mapper/cryptroot
        mkfs.$ROOT_FILESYSTEM_990 -f /dev/mapper/cryptroot
        mkfs.vfat -F32 /dev/vda1
        mount /dev/mapper/cryptroot /mnt
        mkdir $BOOT_MOUNT_LOCATION_990
        mount /dev/vda1 /mnt/boot
    


clear

# Pacstrap stuff
display_packstrapping_packages

# KEYRING AURGMENTATION
# Check if keyring directory exists
if [ -d "/etc/pacman.d/gnupg" ]; then
    echo "Existing keyring directory found. Removing..."
    sudo rm -r "/etc/pacman.d/gnupg"
fi

# Initialize and populate keyring - A requirement for first-run
echo "Initializing new keyring..."
sudo pacman-key --init

echo "Populating keyring with Arch Linux keys..."
sudo pacman-key --populate archlinux

echo "Keyring setup complete."

# REFLECTOR MIRROR LIST UPDATE
reflector --latest 10 --protocol https --sort rate --save /etc/pacman.d/mirrorlist
pacstrap /mnt $SYNSTALL

# GENFSTAB -- BOOT STUFFd
genfstab -U /mnt >> /mnt/etc/fstab

# COPY ROOT OVERLAY MATERIALS
cp -R /root/SYN-ROOTOVERLAY/* $ROOT_MOUNT_LOCATION_990/

# COPY chroot wrap-up script to finalise install...
mkdir /mnt/root/
cp -R /root/SYN-INSTALLER-SCRIPTS/syn-1_chroot.sh $ROOT_MOUNT_LOCATION_990/root/

# NOTIFICATION: Entering Stage 1
echo "NOTIFICATION: Stage Zero Complete - Entering Stage 1"
arch-chroot $ROOT_MOUNT_LOCATION_990 chmod +x /root/syn-1_chroot.sh
arch-chroot $ROOT_MOUNT_LOCATION_990 /root/syn-1_chroot.sh
