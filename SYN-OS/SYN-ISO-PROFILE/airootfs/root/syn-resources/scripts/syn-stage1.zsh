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

# Load disk configuration variables defined in syn-disk-config.zsh
DISK_CONFIG_FILE="/root/syn-resources/scripts/syn-disk-config.zsh"
if [ -f "$DISK_CONFIG_FILE" ]; then
    # shellcheck disable=SC1090
    source "$DISK_CONFIG_FILE"
fi

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

# Ensure disk vars exist if config was not sourced
if [ -z "$WIPE_DISK_990" ]; then
    WIPE_DISK_990="/dev/vda"
fi

# Derive partition defaults only if not provided
if [ "$SYNOS_ENV" = "UEFI" ]; then
    BOOTLOADER="systemd-boot"
    : "${BOOT_PART_990:=${WIPE_DISK_990}1}"
    : "${ROOT_PART_990:=${WIPE_DISK_990}2}"
else
    BOOTLOADER="Syslinux"
    # MBR layout has a single root partition by default
    : "${ROOT_PART_990:=${WIPE_DISK_990}1}"
    # BOOT_PART_990 is not used for MBR installs
fi

# Display configuration settings
    echo ""
    printf "\033[32m• Root: %s mounted at %s\033[0m\n" "$ROOT_PART_990" "$ROOT_MOUNT_LOCATION_990"
    if [ "$SYNOS_ENV" = "UEFI" ]; then
        printf "\033[32m• Boot: %s mounted at %s (fs=%s)\033[0m\n" "$BOOT_PART_990" "$BOOT_MOUNT_LOCATION_990" "$BOOT_FILESYSTEM_990"
    fi
    printf "\033[32m• Root FS: %s\033[0m\n" "$ROOT_FILESYSTEM_990"
    printf "\033[32m• fstab generated, packages installed, scripts copied.\033[0m\n\n"
    sleep 2
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
echo " - Network Interface Detected: ${NETWORK_INTERFACE_990:-none}"
echo " - Bootloader: $BOOTLOADER"
echo " - Root Partition: $ROOT_PART_990"
[ -n "$BOOT_PART_990" ] && echo " - Boot Partition: $BOOT_PART_990"
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

    # Ensure custom SYN‑OS scripts are executable.  When the skeleton is
    # copied into the new user’s home and Polybar launch
    # script may not have execute permissions by default.  Fix them here.

    if [ -f "/home/$DEFAULT_USER_990/.config/polybar/launch.sh" ]; then
        chmod +x "/home/$DEFAULT_USER_990/.config/polybar/launch.sh" 2>/dev/null || true
    fi

# Enable systemd services for DHCP and Wi-Fi
echo "Enabling systemd services for networking"
if [ -n "$NETWORK_INTERFACE_990" ]; then
    systemctl enable "dhcpcd@${NETWORK_INTERFACE_990}.service" || systemctl enable dhcpcd.service
else
    systemctl enable dhcpcd.service
fi
systemctl enable iwd.service

# Bootloader setup based on SYNOS_ENV
if [ "$SYNOS_ENV" = "UEFI" ]; then
    echo "Configuring systemd-boot for UEFI system"
    bootctl --path=/boot install

    # Get UUID of the root partition
    ROOT_REAL_UUID_990=$(blkid -s UUID -o value "$ROOT_PART_990")

    # Write bootloader configurations
    echo "default  syn.conf" > /boot/loader/loader.conf
    echo "timeout  0" >> /boot/loader/loader.conf
    echo "editor   0" >> /boot/loader/loader.conf

    mkdir -p /boot/loader/entries
    {
        echo "title    SYN-OS"
        echo "linux    /vmlinuz-linux"
        echo "initrd   /initramfs-linux.img"
        echo "options  root=UUID=$ROOT_REAL_UUID_990 rw"
    } > /boot/loader/entries/syn.conf

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
    ROOT_REAL_UUID_990=$(blkid -s UUID -o value "$ROOT_PART_990")

    # Configure syslinux.cfg to use root UUID
    # Replace any existing root=... with root=UUID=<uuid>
    if [ -f /boot/syslinux/syslinux.cfg ]; then
        sed -i -E "s|root=[^ ]+|root=UUID=$ROOT_REAL_UUID_990|g" /boot/syslinux/syslinux.cfg
        echo "Syslinux configuration updated with root UUID."
    else
        echo "Warning: /boot/syslinux/syslinux.cfg not found."
    fi
fi

# Install microcode packages (optional, recommended for stability)
echo "Installing microcode packages for CPU"
CPU_VENDOR=$(lscpu | awk -F: '/Vendor ID:/ {gsub(/^ +| +$/,\"\",$2); print $2}')
if [ "$CPU_VENDOR" = "GenuineIntel" ]; then
    pacman -S --noconfirm intel-ucode
elif [ "$CPU_VENDOR" = "AuthenticAMD" ]; then
    pacman -S --noconfirm amd-ucode
fi

# Enable multilib repository if needed
if ! grep -q "^\[multilib\]" /etc/pacman.conf; then
    echo "Enabling multilib repository"
    printf "\n[multilib]\nInclude = /etc/pacman.d/mirrorlist\n" >> /etc/pacman.conf
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
Please ensure that your computer's BIOS or UEFI or Virtual Machine is configured to boot from the newly installed disk.
After verifying the boot configuration, you can now exit and reboot to start using SYN-OS.

To exit the chroot environment, type: exit
Then, to reboot the system, type: reboot
"
