#!/bin/zsh

# ------------------------------------------------------------------------------
#                           SYN-OS Stage 1 Script
#   Finalizes SYN-OS installation within the chroot environment by configuring
#   system settings, installing bootloader, and creating user accounts.
#   Adapts to UEFI or MBR systems based on the SYNOS_ENV variable passed from
#   stage0.
#
#   SYN-OS        : The Syntax Operating System
#   Author        : William Hayward-Holland
#   License       : MIT License
# ------------------------------------------------------------------------------

clear

# Main script variables
echo "Setting up new system variables"

# Define variables
DEFAULT_USER_990="syntax990"                                                # Sets the default username
FINAL_HOSTNAME_990="SYN-TESTBUILD"                                          # Sets the OS hostname
LOCALE_GEN_990="en_GB.UTF-8 UTF-8"                                          # Sets the locale generation
LOCALE_CONF_990="LANG=en_GB.UTF-8"                                          # Sets the locale configuration and language
ZONE_INFO990="GB"                                                           # Sets the zone information
SHELL_CHOICE_990="/bin/zsh"                                                 # Sets the default shell choice
NETWORK_INTERFACE_990=$(ip -o -4 route show to default | awk '{print $5}')  # Sets the network interface

# Detect SYNOS_ENV to determine UEFI or MBR setup
if [ -z "$SYNOS_ENV" ]; then
    echo "Error: SYNOS_ENV variable is not set. Exiting."
    exit 1
fi

# Set ROOT_PART_990 based on SYNOS_ENV
if [ "$SYNOS_ENV" = "UEFI" ]; then
    ROOT_PART_990="/dev/sda2"
    BOOTLOADER="systemd-boot"
    BOOT_PART_990="/dev/sda1"
else
    ROOT_PART_990="/dev/sda1"
    BOOTLOADER="Syslinux"
fi

# Display configuration settings
echo "Proceeding to finalize installation."
echo
echo "Configuration settings:"
sleep 0.2
echo " - Username: $DEFAULT_USER_990"
echo " - Hostname: $FINAL_HOSTNAME_990"
echo " - Locale Generation: $LOCALE_GEN_990"
echo " - Locale Configuration and Language: $LOCALE_CONF_990"
echo " - Zone: $ZONE_INFO990"
echo " - Shell: $SHELL_CHOICE_990"
echo " - Network Interface Detected: $NETWORK_INTERFACE_990"
echo " - Bootloader: $BOOTLOADER"
echo ""
echo "PRESS CTRL+C TO ABORT RIGHT NOW IF THESE ARE INCORRECT"
sleep 3

# Set hardware clock to system time
echo "Setting hardware clock to system time"
hwclock --systohc

# Remove any existing locale.gen file
echo "Removing existing locale.gen file"
rm -f /etc/locale.gen

# Create new locale.gen file with locale generation
echo "$LOCALE_GEN_990" > /etc/locale.gen
locale-gen

# Create new locale.conf file with locale configuration
echo "$LOCALE_CONF_990" > /etc/locale.conf

# Create new hostname file with final hostname
echo "$FINAL_HOSTNAME_990" > /etc/hostname

# Create symbolic link for timezone information
ln -sf "/usr/share/zoneinfo/$ZONE_INFO990" /etc/localtime

# Modify sudoers file to allow members of the wheel group to use sudo
echo "%wheel ALL=(ALL) ALL" > /etc/sudoers.d/990_wheel
chmod 440 /etc/sudoers.d/990_wheel

# Create the user account, assign default shell, and set password
echo "Creating user $DEFAULT_USER_990's account"
useradd -m -G wheel -s "$SHELL_CHOICE_990" "$DEFAULT_USER_990"
echo "Set password for user $DEFAULT_USER_990:"
passwd "$DEFAULT_USER_990"
chown -R "$DEFAULT_USER_990:$DEFAULT_USER_990" "/home/$DEFAULT_USER_990"

# Enable systemd services for DHCP and Wi-Fi
echo "Enabling systemd services for networking"
systemctl enable "dhcpcd@$NETWORK_INTERFACE_990.service"
systemctl enable iwd.service

# Bootloader setup based on SYNOS_ENV
if [ "$SYNOS_ENV" = "UEFI" ]; then
    echo "Configuring systemd-boot for UEFI system"
    bootctl --path=/boot install

    # Get UUID of the root partition
    ROOT_REAL_UUID_990=$(blkid -s UUID -o value $ROOT_PART_990)

    # Write bootloader configurations
    echo "default  syn.conf" > /boot/loader/loader.conf
    echo "timeout  0" >> /boot/loader/loader.conf
    echo "editor   0" >> /boot/loader/loader.conf

    mkdir -p /boot/loader/entries
    echo "title    SYN-OS" > /boot/loader/entries/syn.conf
    echo "linux    /vmlinuz-linux" >> /boot/loader/entries/syn.conf
    echo "initrd   /initramfs-linux.img" >> /boot/loader/entries/syn.conf
    echo "options  root=UUID=$ROOT_REAL_UUID_990 rw" >> /boot/loader/entries/syn.conf

    # Configure mkinitcpio
    sed -i 's/^HOOKS=.*/HOOKS=(base udev autodetect modconf block filesystems keyboard fsck)/' /etc/mkinitcpio.conf
    mkinitcpio -P
else
    echo "Configuring Syslinux for MBR system"

    # Install Syslinux to the MBR
    syslinux-install_update -i -a -m
    if [ $? -ne 0 ]; then
        echo "Failed to install Syslinux"
        exit 1
    fi

    # Get UUID of the root partition
    ROOT_REAL_UUID_990=$(blkid -s UUID -o value $ROOT_PART_990)

    # Configure syslinux.cfg
    sed -i "s|root=/dev/sda3|root=UUID=$ROOT_REAL_UUID_990|" /boot/syslinux/syslinux.cfg
    echo "Syslinux configuration updated with root UUID."
fi

# Install microcode packages (optional, recommended for stability)
echo "Installing microcode packages for CPU"
CPU_VENDOR=$(lscpu | grep "Vendor ID:" | awk '{print $3}')
if [ "$CPU_VENDOR" = "GenuineIntel" ]; then
    pacman -S --noconfirm intel-ucode
elif [ "$CPU_VENDOR" = "AuthenticAMD" ]; then
    pacman -S --noconfirm amd-ucode
fi

# Enable multilib repository if needed
if ! grep -q "\[multilib\]" /etc/pacman.conf; then
    echo "Enabling multilib repository"
    echo -e "\n[multilib]\nInclude = /etc/pacman.d/mirrorlist" >> /etc/pacman.conf
    pacman -Syy
fi

# Install additional packages as per your requirements
echo "Installing additional packages"
pacman -S --noconfirm vim nano net-tools dnsutils

# Clean up package cache
pacman -Scc --noconfirm

sleep 0.5
clear

# Display final instructions
echo "
████████████████████████████████████████████████████████████████████████████████████████████
                                                                                                ██
  ██████▓██   ██▓ ███▄    █  ▒█████    ██████                                                 ██
▒██    ▒ ▒██  ██▒ ██ ▀█   █ ▒██▒  ██▒▒██    ▒                                                 ██
░ ▓██▄    ▒██ ██░▓██  ▀█ ██▒▒██░  ██▒░ ▓██▄                                                   ██
  ▒   ██▒ ░ ▐██▓░▓██▒  ▐▌██▒▒██   ██░  ▒   ██▒      ______ _  _  _ _   _        _________     ██
▒██████▒▒ ░ ██▒▓░▒██░   ▓██░░ ████▓▒░▒██████▒▒      \\  ___) || || | \\ | |      / _ \\  ___)    ██
▒ ▒▓▒ ▒ ░  ██▒▒▒ ░ ▒░   ▒ ▒ ░ ▒░▒░▒░ ▒ ▒▓▒ ▒ ░       \\ \\  | \\| |/ |  \\| |_____| | | \\ \\       ██
░ ░▒  ░ ░▓██ ░▒░ ░ ░░   ░ ▒░  ░ ▒ ▒░ ░ ░▒  ░ ░        > >  \\_   _/|     (_____) | | |> >      ██
░  ░  ░  ▒ ▒ ░░     ░   ░ ░ ░ ░ ░ ▒  ░  ░  ░         / /__   | |  | |\\  |     | |_| / /__     ██
      ░  ░ ░              ░     ░ ░        ░        /_____)  |_|  |_| \\_|      \\___/_____)    ██
         ░ ░                                                                                  ██
                                                                                                ██
                          01010011 01011001 01001110 00101101 01001111 01010011               ██
                                                                                                ██
                                           SYN-OS: The Syntax Operating System                ██
 ####  #### #   #      ###   ####                                                             ██
#     #   # #   #     #   # #                                                                 ██
#      #### ##### ### #   # #              Created By: ----¬                                  ██
#      #  # #   #     #   # #                              :                                  ██
 #### #   # #   #      ###   ####                          :                                  ██
                                                          ===                                 ██
                                                                                                ██
███████ ██    ██ ███    ██ ████████  █████  ██   ██  █████   █████   ██████                   ██
██       ██  ██  ████   ██    ██    ██   ██  ██ ██  ██   ██ ██   ██ ██  ████                  ██
███████   ████   ██ ██  ██    ██    ███████   ███    ██████  ██████ ██ ██ ██                  ██
     ██    ██    ██  ██ ██    ██    ██   ██  ██ ██       ██      ██ ████  ██                  ██
███████    ██    ██   ████    ██    ██   ██ ██   ██  █████   █████   ██████                   ██
                                                                                                ██
████████████████████████████████████████████████████████████████████████████████████████████████
"

# Display final instructions
echo "
SUMMARY: Stage One Complete, Congratulations! You have successfully installed SYN-OS with $BOOTLOADER bootloader.
Please ensure that your computer's BIOS/UEFI or Virtual Machine is configured to boot from the newly installed disk.
After verifying the boot configuration, you can now exit and reboot to start using SYN-OS.

To exit the chroot environment, type: exit
Then, to reboot the system, type: reboot
"
