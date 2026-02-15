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

# --- Begin: ensure sourced vars exist (safe defaults for dev/chroot runs) ---
: "${DISK:=/dev/sda}"
: "${ROOT_MOUNT_LOCATION:=/mnt}"
: "${BOOT_MOUNT_LOCATION:=/mnt/boot}"
: "${ROOT_FS:=ext4}"
: "${BOOT_FS:=fat32}"
# --- End: defaults ---

echo "Setting up new system variables"

# -----------------------------------------------------------------------------#
# Simple, local config (EDIT THESE HERE FOR YOUR PERSONAL PREFERENCES)
# -----------------------------------------------------------------------------#
DEFAULT_USER="syntax990"
FINAL_HOSTNAME="SYN-TESTBUILD"
LOCALE_GEN="en_GB.UTF-8 UTF-8"
LOCALE_CONF="LANG=en_GB.UTF-8"
ZONE_INFO="GB"
SHELL_CHOICE="/bin/zsh"
NETWORK_INTERFACE="$(ip -o -4 route show to default 2>/dev/null | awk '{print $5}')"
# -----------------------------------------------------------------------------#
# -----------------------------------------------------------------------------#
# Detect SYNOS_ENV to determine UEFI or MBR setup
if [ -z "$SYNOS_ENV" ]; then
    echo "Error: SYNOS_ENV variable is not set. Exiting."
    exit 1
fi

# Derive partition defaults based on DISK and SYNOS_ENV
if [ "$SYNOS_ENV" = "UEFI" ]; then
    BOOTLOADER="systemd-boot"
    : "${BOOT_PART:=${DISK}1}"
    : "${ROOT_PART:=${DISK}2}"
else
    BOOTLOADER="Syslinux"
    : "${ROOT_PART:=${DISK}1}"
    # BOOT_PART unused for MBR installs
fi

# Display configuration settings
echo ""
printf "\033[32m• Root: %s mounted at %s\033[0m\n" "$ROOT_PART" "$ROOT_MOUNT_LOCATION"
if [ "$SYNOS_ENV" = "UEFI" ]; then
    printf "\033[32m• Boot: %s mounted at %s (fs=%s)\033[0m\n" "$BOOT_PART" "$BOOT_MOUNT_LOCATION" "$BOOT_FS"
fi
printf "\033[32m• Root FS: %s\033[0m\n" "$ROOT_FS"
printf "\033[32m• fstab generated, packages installed, scripts copied.\033[0m\n\n"
sleep 2

echo "Proceeding to finalize installation."
echo
echo "Configuration settings:"
sleep 0.2
echo " - Username: $DEFAULT_USER"
echo " - Hostname: $FINAL_HOSTNAME"
echo " - Locale Generation: $LOCALE_GEN"
echo " - Locale Configuration and Language: $LOCALE_CONF"
echo " - Zone: $ZONE_INFO"
echo " - Shell: $SHELL_CHOICE"
echo " - Network Interface Detected: ${NETWORK_INTERFACE:-none}"
echo " - Bootloader: $BOOTLOADER"
echo " - Root Partition: $ROOT_PART"
[ -n "$BOOT_PART" ] && echo " - Boot Partition: $BOOT_PART"
echo ""
echo "PRESS CTRL+C TO ABORT RIGHT NOW IF THESE ARE INCORRECT"
sleep 3

# Set hardware clock to system time
echo "Setting hardware clock to system time"
hwclock --systohc

# Locale setup
echo "Removing existing locale.gen file"
rm -f /etc/locale.gen
echo "$LOCALE_GEN" > /etc/locale.gen
locale-gen
echo "$LOCALE_CONF" > /etc/locale.conf

# Hostname and timezone
echo "$FINAL_HOSTNAME" > /etc/hostname
ln -sf "/usr/share/zoneinfo/$ZONE_INFO" /etc/localtime

# Sudoers for wheel
echo "%wheel ALL=(ALL) ALL" > /etc/sudoers.d/990_wheel
chmod 440 /etc/sudoers.d/990_wheel

# User account
echo "Creating user $DEFAULT_USER's account"
useradd -m -G wheel -s "$SHELL_CHOICE" "$DEFAULT_USER"
echo "Set password for user $DEFAULT_USER:"
passwd "$DEFAULT_USER"
chown -R "$DEFAULT_USER:$DEFAULT_USER" "/home/$DEFAULT_USER"

# Ensure custom SYN‑OS scripts are executable (if present)
chmod +x "/home/$DEFAULT_USER/.config/waybar/daynight-obmenu.sh"
chmod +x "/home/$DEFAULT_USER/.config/ranger/scope.sh"

# Networking
echo "Enabling systemd services for networking"
if [ -n "$NETWORK_INTERFACE" ]; then
    systemctl enable "dhcpcd@${NETWORK_INTERFACE}.service" || systemctl enable dhcpcd.service
else
    systemctl enable dhcpcd.service
fi
systemctl enable iwd.service

# Bootloader setup based on SYNOS_ENV
if [ "$SYNOS_ENV" = "UEFI" ]; then
    echo "Configuring systemd-boot for UEFI system"
    bootctl --path=/boot install

    # Root UUID
    ROOT_UUID="$(blkid -s UUID -o value "$ROOT_PART")"

    # systemd-boot configs
    echo "default  syn.conf" > /boot/loader/loader.conf
    echo "timeout  0" >> /boot/loader/loader.conf
    echo "editor   0" >> /boot/loader/loader.conf

    mkdir -p /boot/loader/entries
    {
        echo "title    SYN-OS"
        echo "linux    /vmlinuz-linux"
        echo "initrd   /initramfs-linux.img"
        echo "options  root=UUID=$ROOT_UUID rw"
    } > /boot/loader/entries/syn.conf

    # mkinitcpio
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

    # Root UUID
    ROOT_UUID="$(blkid -s UUID -o value "$ROOT_PART")"

    # Update syslinux.cfg to use root UUID
    if [ -f /boot/syslinux/syslinux.cfg ]; then
        sed -i -E "s|root=[^ ]+|root=UUID=$ROOT_UUID|g" /boot/syslinux/syslinux.cfg
        echo "Syslinux configuration updated with root UUID."
    else
        echo "Warning: /boot/syslinux/syslinux.cfg not found."
    fi
fi

sleep 0.5
clear

# Banner
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

# Final instructions
cat <<EOF

SUMMARY: Stage One Complete, Congratulations! 

You have successfully installed SYN-OS with $BOOTLOADER bootloader.

Please ensure that your computer's BIOS or UEFI or Virtual Machine is configured to boot from the newly installed disk.
After verifying the boot configuration, you can now exit and reboot to start using SYN-OS.

To reboot the system, type: reboot

EOF