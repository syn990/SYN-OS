#!/bin/bash

# Set up keyboard layout, synchronize time and set up DHCP
setup_system() {
    loadkeys uk
    timedatectl set-ntp true
    systemctl start dhcpcd.service
}

# Partition the disk, format the partitions, and mount them
partition_disk() {
    local wipe_disk="$1"
    local boot_part="$2"
    local root_part="$3"
    local boot_mount="$4"
    local root_mount="$5"
    
    parted --script "$wipe_disk" mklabel gpt \
        mkpart primary fat32 1Mib 200Mib set 1 boot on \
        mkpart primary ext4 201Mib 100%
    mkfs.vfat -F 32 "$boot_part"
    mkfs.ext4 -F "$root_part"
    mount "$root_part" "$root_mount"
    mkdir "$boot_mount"
    mount "$boot_part" "$boot_mount"
}

# Install packages and secure keyring
install_packages() {
    local packages=("$@")
    
    # Refresh mirror list and update package databases
    echo "Refreshing mirror list and updating package databases..."
    reflector -c "GB" -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist
    pacman -Syy
    
    # Initialize and populate the Pacman keyring
    echo "Initializing and populating the Pacman keyring..."
    pacman-key --init
    pacman-key --populate archlinux
    
    # Install packages
    echo "Installing packages..."
    if ! pacman -S --noconfirm "${packages[@]}"; then
        echo "Error: Failed to install packages"
        exit 1
    fi
}


# Generate filesystem table with boot information in respect to UUID assignment
generate_filesystem_table() {
    genfstab -U /mnt >> /mnt/etc/fstab
}

# Copy root filesystem overlay materials to the result system root directory
copy_overlay() {
    local root_overlay="$1"
    cp -R "$root_overlay"/* /mnt/
    cp -R syn-stage1.sh /mnt/root/syn-stage1.sh
}

# Provide instructions for continuing the installation process
print_instructions() {
    local root_mount="$1"
    echo "Stage Zero Complete - You now need to arch-chroot into $root_mount"
}

# Main function
main() {
    local wipe_disk="/dev/sda"
    local boot_part="/dev/sda1"
    local root_part="/dev/sda2"
    local boot_mount="/mnt/boot"
    local root_mount="/mnt"

    local base_packages="base base-devel dosfstools fakeroot gcc linux linux-firmware pacman-contrib sudo zsh"
    local system_packages="alsa-utils archlinux-xdg-menu dhcpcd dnsmasq hostapd iwd pulseaudio python-pyalsa"
    local control_packages="lxrandr obconf-qt pavucontrol-qt"
    local wm_packages="openbox xcompmgr xorg-server xorg-xinit tint2"
    local cli_packages="git htop man nano reflector rsync wget"
    local gui_packages="engrampa feh kitty kwrite pcmanfm-qt"
    local font_packages="terminus-font ttf-bitstream-vera"
    local cli_extra_packages="android-tools archiso binwalk brightnessctl hdparm hexedit lshw ranger sshfs yt-dlp"
    local gui_extra_packages="audacity chromium gimp kdenlive obs-studio openra spectacle vlc"
    local vm_extra_packages="edk2-ovmf libvirt qemu-desktop virt-manager virt-viewer"

   
    local all_packages=("$base_packages" "$system_packages" "$control_packages" "$wm_packages" "$cli_packages" "$gui_packages" "$font_packages" "$cli_extra_packages" "$gui_extra_packages" "$vm_extra_packages")

    setup_system
    partition_disk "$wipe_disk" "$boot_part" "$root_part" "$boot_mount" "$root_mount"
    install_packages "${all_packages[@]}"
    generate_filesystem_table
    copy_overlay "/root/SYN-RTOS-V3/1.root_filesystem_overlay"
    print_instructions "$root_mount"
}

main
