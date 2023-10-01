#!/bin/bash

# SYN-INSTALLER-MAIN.SH
# This is the main script to be executed when you begin the installation of SYN-OS.
# Variables and other resources are deposited across the ISO. It's all a mess.
# All variables, functions, and processes will branch from this script.

# Installation:
# This script aims to do the following: (/root/SYN-INSTALLER-SCRIPTS/*)
# - Map defined partition, file-system, and mounting information seen in syn-disk-variables.sh
# - Pacstrap using packages described in syn-pacstrap-variables.sh

# Load additional sources
source /root/SYN-INSTALLER-SCRIPTS/syn-installer-functions.sh
source /root/SYN-INSTALLER-SCRIPTS/syn-disk-variables.sh
source /root/SYN-INSTALLER-SCRIPTS/syn-pacstrap-variables.sh
source /root/SYN-INSTALLER-SCRIPTS/syn-ascii-art.sh

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

# Check if EFI variables are present to wipe with GPT, otherwise run the MBR fallback
if [ -d "/sys/firmware/efi/efivars" ]; then
    echo "EFI variables detected. Proceeding with GPT partitioning..."
    echo ""
    
    # Announce and declare variables
    printf "\033[1;33m%s\033[0m\n" "THIS IS A SIMULATION... CHECK THE SOURCE AND WHEN IT WORKS UPDATE THIS PROMPT(S)"
    sleep 5


    parted --script $WIPE_DISK_990 mklabel gpt mkpart primary $BOOT_FILESYSTEM_990 1MiB 200MiB set 1 boot on
    check_success "Failed to create boot partition"

    echo "Creating root partition: $BOOT_PART_990"
    parted --script $WIPE_DISK_990 mkpart primary $ROOT_FILESYSTEM_990 201MiB 100%
    check_success "Failed to create root partition"

    echo "Formatting boot partition: $BOOT_PART_990"
    mkfs.vfat -F32 $BOOT_PART_990
    check_success "Failed to format boot partition"

    echo "Formatting root partition: $ROOT_PART_990"
    mkfs.$ROOT_FILESYSTEM_990 -f $ROOT_PART_990
    check_success "Failed to format root partition"

    mount $ROOT_PART_990 $ROOT_MOUNT_LOCATION_990
    check_success "Failed to mount root partition"

    echo "Mounting root partition: $ROOT_PART_990 to $ROOT_MOUNT_LOCATION_990"
    mkdir $BOOT_MOUNT_LOCATION_990
    check_success "Failed to create boot directory"

    echo "Creating boot directory: $BOOT_MOUNT_LOCATION_990"
    mount $BOOT_PART_990 $BOOT_MOUNT_LOCATION_990
    check_success "Failed to mount boot partition"

    echo "Mounting boot partition: $BOOT_PART_990 to $BOOT_MOUNT_LOCATION_990"

    # Add code for disk encryption with LUKS
    echo "Setting up LUKS encryption for the root partition: $ROOT_PART_990"
    
    umount $BOOT_PART_990
    umount $ROOT_PART_990
    
    cryptsetup luksFormat $WIPE_DISK_990
    cryptsetup luksDump $WIPE_DISK_990
    cryptsetup -s 512 luksFormat $ROOT_PART_990
    cryptsetup open $ROOT_PART_990 root 
    check_success "Failed to format root partition with LUKS"
    
    cryptsetup open $ROOT_PART_990 cryptroot
    check_success "Failed to open encrypted root partition"
    
    # Format the encrypted root partition with the desired file system
    mkfs.$ROOT_FILESYSTEM_990 /dev/mapper/cryptroot
    check_success "Failed to format encrypted root partition"
    
    ROOT_PART_990="/dev/mapper/cryptroot"  # Update root partition variable
    
    echo "Root partition encrypted successfully."
    
else
    echo "EFI variables not detected. Proceeding with MBR partitioning..."
    echo ""

    parted --script $WIPE_DISK_990 mklabel msdos mkpart primary $ROOT_FILESYSTEM_990 1MiB 100%
    check_success "Failed to create root partition"

    parted --script $WIPE_DISK_990 set 1 boot on
    check_success "Failed to set boot flag for the root partition"

    echo "Formatting root partition: $ROOT_PART_990"
    mkfs.$ROOT_FILESYSTEM_990 $ROOT_PART_990
    check_success "Failed to format root partition"

    mount $ROOT_PART_990 $ROOT_MOUNT_LOCATION_990
    check_success "Failed to mount root partition"

    echo "Mounting root partition: $ROOT_PART_990 to $ROOT_MOUNT_LOCATION_990"
fi

clear

# Pacstrap stuff
display_packstrapping_packages

# KEYRING AUGMENTATION
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

# GENFSTAB -- BOOT STUFF
genfstab -U /mnt >> /mnt/etc/fstab

# COPY ROOT OVERLAY MATERIALS
cp -R /root/SYN-ROOTOVERLAY/* $ROOT_MOUNT_LOCATION_990
cp -R /root/SYN-INSTALLER-SCRIPTS/syn-1_chroot-encrupted.sh $ROOT_MOUNT_LOCATION_990

# NOTIFICATION: Entering Stage 1
echo "NOTIFICATION: Stage Zero Complete - Entering Stage 1"
echo ""
echo "Congratulations! You have successfully completed Stage Zero of the process. Now, we will proceed to the next steps. Please note the following instructions:"
echo ""
echo "To continue building the system, the next command to run is: arch-chroot $ROOT_MOUNT_LOCATION_990/root/syn-1_chroot.sh"
echo ""
echo "During Stage Zero, the following tasks were completed:"
echo "1. The root partition ($ROOT_PART_990) was mounted to the root directory ($ROOT_MOUNT_LOCATION_990)."
if [ -d "/sys/firmware/efi/efivars" ]; then
    echo "2. The boot partition ($BOOT_PART_990) was created and formatted with the appropriate filesystem mentioned in syn-stage0.sh: ($BOOT_FILESYSTEM_990)."
else
    echo "2. The root partition ($ROOT_PART_990) was created and formatted with the appropriate filesystem mentioned in syn-stage0.sh: ($ROOT_FILESYSTEM_990)."
fi
echo "3. The boot partition ($BOOT_PART_990) was mounted to the boot directory ($BOOT_MOUNT_LOCATION_990)."
echo "4. The filesystem table with boot information was generated, including UUID assignment."
echo "5. Essential packages were installed to the resulting system using Pacstrap."
echo "6. Mirror mystics were applied, and the keyring was re-secured."
echo "7. Cryptographic keys for Pacman were generated, and the package database was updated."
echo "8. The root overlay materials from $ROOT_OVERLAY_DIRECTORY were copied to the root directory."

arch-chroot $ROOT_MOUNT_LOCATION_990 chmod +x /syn-1_chroot-encrypted.sh
arch-chroot $ROOT_MOUNT_LOCATION_990 /syn-1_chroot-encrypted.sh
