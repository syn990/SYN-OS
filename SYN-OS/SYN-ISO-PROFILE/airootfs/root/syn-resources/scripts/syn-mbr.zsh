#!/bin/zsh

# SYN-OS
# SYNTAX990
# WILLIAM HAYWARD-HOLLAND
# M.I.T LICENSE

# - syn-stage1.zsh - Stage 1 MBR

# Requires fixing as to not define ROOT_PART_990 twice during process
ROOT_PART_990="/dev/sda1"
# source /syn-stage0.zsh

clear

# Main script variables
echo "Setting up new system variables" 

# Define variables
DEFAULT_USER_990="syntax990"                                                # Sets the default username
FINAL_HOSTNAME_990="SYN-TESTBUILD"                                          # Sets the OS hostname
LOCALE_GEN_990="en_GB.UTF-8 UTF-8"                                          # Sets the locale generation 
LOCALE_CONF_990="LANG=en_GB.UTF-8"                                          # Sets the locale configuration and language
ZONE_INFO990="GB"                                                           # Sets the zone information
SHELL_CHOICE_990="/bin/zsh"                                                 # Sets the default shell choice (e.g., /bin/bash)
NETWORK_INTERFACE_990=$(ip -o -4 route show to default | awk '{print $5}')  # Sets the network interface

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
echo " - IP Detected: $NETWORK_INTERFACE_990"
echo ""
echo "PRESS CTRL+C TO ABORT RIGHT NOW IF THESE ARE INCORRECT "
sleep 3

# Set hardware clock to system time
echo "Setting hardware clock to system time"
hwclock --systohc

# Remove any garbled data or prematurely created locale.gen
echo "Removing existing locale.gen file"
rm /etc/locale.gen

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

# Create the user account, assign default shell, and take ownership of home directory and files
echo "Creating user $DEFAULT_USER_990's account"
useradd -m -G wheel -s "$SHELL_CHOICE_990" "$DEFAULT_USER_990"
passwd "$DEFAULT_USER_990"
chown "$DEFAULT_USER_990:$DEFAULT_USER_990" -R "/home/$DEFAULT_USER_990"

# Enable systemd services for DHCP, WiFi, and set up the bootloader
echo "Enabling systemd services and setting up bootloader"
systemctl enable "dhcpcd@$NETWORK_INTERFACE_990.service"
systemctl enable iwd.service

#Install bootloader

       #SYSLINUX (mbr)
#      mkdir /boot/syslinux/                #created dummy folder in syn-stage0.sh
#       syslinux-install_update -i -a -m     #install syslinux to mbr
       echo "You are on your own - manually correct the file"
       nano /boot/syslinux/syslinux.cfg     #force manual correction of syslinux sda1 parameter

# Display final instructions
echo "
████████████████████████████████████████████████████████████████████████████████████████████
                                                                                              ██
  ██████▓██   ██▓ ███▄    █  ▒█████    ██████                                                 ██
▒██    ▒ ▒██  ██▒ ██ ▀█   █ ▒██▒  ██▒▒██    ▒                                                 ██
░ ▓██▄    ▒██ ██░▓██  ▀█ ██▒▒██░  ██▒░ ▓██▄                                                   ██
  ▒   ██▒ ░ ▐██▓░▓██▒  ▐▌██▒▒██   ██░  ▒   ██▒      ______ _  _  _ _   _        _________     ██
▒██████▒▒ ░ ██▒▓░▒██░   ▓██░░ ████▓▒░▒██████▒▒      \  ___) || || | \ | |      / _ \  ___)    ██
▒ ▒▓▒ ▒ ░  ██▒▒▒ ░ ▒░   ▒ ▒ ░ ▒░▒░▒░ ▒ ▒▓▒ ▒ ░       \ \  | \| |/ |  \| |_____| | | \ \       ██
░ ░▒  ░ ░▓██ ░▒░ ░ ░░   ░ ▒░  ░ ▒ ▒░ ░ ░▒  ░ ░        > >  \_   _/|     (_____) | | |> >      ██
░  ░  ░  ▒ ▒ ░░     ░   ░ ░ ░ ░ ░ ▒  ░  ░  ░         / /__   | |  | |\  |     | |_| / /__     ██
      ░  ░ ░              ░     ░ ░        ░        /_____)  |_|  |_| \_|      \___/_____)    ██
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
SUMMARY: Stage Two Complete, Congratulations! You have successfully installed SYN-OS with SYSLINUX bootloader.
Please ensure that your computer's BIOS or Virtual Machine is configured to boot from the newly installed disk.
After verifying the boot configuration, you can now exit and reboot to start using SYN-OS.
"
