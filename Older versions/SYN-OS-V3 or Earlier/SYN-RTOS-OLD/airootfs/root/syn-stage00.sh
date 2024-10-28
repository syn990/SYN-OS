#!/bin/bash

# WARNING This script has the potential to cause damage to your system if not executed properly.
# Any consequences resulting from the improper use of this script will be solely your responsibility.
# Future developments for this script include the implementation of advanced features such as non-standard file systems such as xfs, zfs, or btrfs for block-device backups and encryption for both the live and persistent environments. However, these features are not currently implemented and should not be expected to work.

# Set keyboard layout
loadkeys uk

# Enable NTP 
timedatectl set-ntp true

# Start DHCP service
systemctl start dhcpcd.service

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
____    __    ____  __  .______    __  .__   __.   _______ 
\   \  /  \  /   / |  | |   _  \  |  | |  \ |  |  /  _____|
 \   \/    \/   /  |  | |  |_)  | |  | |   \|  | |  |  __  
  \            /   |  | |   ___/  |  | |  .    | |  | |_ | 
   \    /\    /    |  | |  |      |  | |  |\   | |  |__| | 
    \__/  \__/     |__| | _|      |__| |__| \__|  \______| 
 ___________    ____  _______ .______     ____    ____ .___________. __    __   __  .__   __.   _______ 
|   ____\   \  /   / |   ____||   _  \    \   \  /   / |           ||  |  |  | |  | |  \ |  |  /  _____|
|  |__   \   \/   /  |  |__   |  |_)  |    \   \/   /   ---|  |---- |  |__|  | |  | |   \|  | |  |  __  
|   __|   \      /   |   __|  |      /      \_    _/       |  |     |   __   | |  | |  .    | |  | |_ | 
|  |____   \    /    |  |____ |  |\  \----.   |  |         |  |     |  |  |  | |  | |  |\   | |  |__| | 
|_______|   \__/     |_______|| _|  ._____|   |__|         |__|     |__|  |__| |__| |__| \__|  \______| 

If you didn't read the source properly you've probably wiped all your precious data...                                                        
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

# Set partition sizes
BOOT_PART_SIZE=200                                 # Size of the boot partition, at 200 MB
ROOT_PART_SIZE=100                                 # Size of the root partition, 
WIPE_DISK=/dev/sda                                 # Drive to be wiped - The main storage medium
BOOT_PART=$WIPE_DISK"1"                            # The boot partition
ROOT_PART=$WIPE_DISK"2"                            # The root partition
BOOT_DATA=/boot                                    # The boot directory
ROOT_DATA=/                                        # The root directory

echo "WARNING: Proceeding will partition and format '$WIPE_DISK'. This operation is IRREVERSIBLE. Confirm disk info is correct. Continue? (y/n)"
read -r CONFIRM
if [[ ! $CONFIRM =~ ^[Yy]$ ]]; then
    exit 1
fi

# Create GPT label and partitions
if ! parted --script $WIPE_DISK mklabel gpt mkpart primary fat32 1Mib $(BOOT_PART_SIZE)Mib set 1 boot on mkpart primary ext4 $(($BOOT_PART_SIZE+1))Mib 100%; then
    echo "Error: Failed to create partitions. Verify the variables and disk information. Use 'lsblk' command for disk list. Restart the process."
    exit 1
fi

# Create file systems, mount partitions and create boot directory
if ! mkfs.vfat -F 32 $BOOT_PART && mkfs.ext4 -F $ROOT_PART && mount $ROOT_PART $ROOT_DATA && mkdir -p $BOOT_DATA && mount $BOOT_PART $BOOT_DATA; then
    echo "Error: Failed to create file systems, mount partitions or create boot directory. Check the variables at the top and ensure the disk information is accurate. You can use the 'lsblk' command to find your disks. The process must be restarted."
    exit 1
fi

echo "Disk partitioning and formatting complete."

# Update mirrors list and sort by speed
reflector --latest 200 --country "GB" --sort rate --save /etc/pacman.d/mirrorlist

echo  " ___  _   ___ ___ _____ ___    _   ___ 
| _ \/_\ / __/ __|_   _| _ \  /_\ | _ |
|  _/ _ \ (__\__ \ | | |   / / _ \|  _/
|_|/_/ \_\___|___/ |_| |_|_\/_/ \_\_|"

echo Installing packages to the resulting system.
echo Applying mirror mystics and re-securing the keyring

# Initialize and populate pacman keyring
pacman-key --init
pacman-key --populate archlinux

# Synchronize package lists
pacman -Syy

# Define package groups
BASE="base base-devel dosfstools fakeroot gcc linux linux-firmware pacman-contrib sudo zsh"
SYS="alsa-utils archlinux-xdg-menu dhcpcd dnsmasq hostapd iwd pulseaudio python-pyalsa"
CTRL="lxrandr obconf-qt pavucontrol-qt"
WM="openbox xcompmgr xorg-server xorg-xinit tint2"
CLI="git htop man nano reflector rsync wget"
GUI="engrampa feh kitty kwrite pcmanfm-qt"
FONT="terminus-font ttf-bitstream-vera"
XTRA_CLI="android-tools archiso binwalk brightnessctl hdparm hexedit lshw ranger sshfs yt-dlp"
XTRA_GUI="audacity chromium gimp kdenlive obs-studio openra spectacle vlc"
XTRA_VM="edk2-ovmf libvirt qemu-desktop virt-manager virt-viewer"

# Install packages
ALL="$BASE $SYS $CTRL $WM $CLI $GUI $FONT $XTRA_CLI $XTRA_GUI $XTRA_VM"
sudo pacstrap /mnt $ALL

# Generates the file system table and appends it to the /etc/fstab file within the mounted partition. The -U flag is used to specify the partition that is currently mounted.
genfstab -U /mnt >> /mnt/etc/fstab 	

# Patch system with SYN-RTOS materials
SYN_RTOS_DIR="/root/SYN-RTOS-V3/1.root_filesystem_overlay/*"
cp -R $SYN_RTOS_DIR /mnt/
cp -R /root/syn-stage1.sh /mnt/root/syn-stage1.sh
