#!/bin/bash

# Function to display SYN-OS ASCII art logo
display_syn_os_logo() {
echo "                                                                                                                       
████████████████████████████████████████████████████████████████████████████████████████████████                                                                                                                       
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
}


# Function to display system wiping ASCII art logo
display_system_wipe_warning() {
  echo "
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

  "
sleep 2
  echo "If you didn't read the source properly, you may risk wiping all your precious data..."
  }

display_packstrapping_packages() {
    # Display ASCII art
    echo "
  _____  _______ _______ _______ _______  ______ _______  _____   _____  _____ __   _  ______
 |_____] |_____| |       |______    |    |_____/ |_____| |_____] |_____]   |   | \  | |  ____
 |       |     | |_____  ______|    |    |    \_ |     | |       |       __|__ |  \_| |_____|
                                                                                             
  _____  _______ _______ _     _ _______  ______ _______ _______                             
 |_____] |_____| |       |____/  |_____| |  ____ |______ |______                             
 |       |     | |_____  |    \_ |     | |_____| |______ ______|                             
                                                                                             
"
    sleep 2
    
    echo "Installing the following:"
    echo $SYNSTALL
}

syn_directory_structure_description() {
    printf "
# The root of the SYN-OS project
SYN-OS
  |
  # Version 4 of the SYN-OS project
  └──SYN-OS-V4
      # Stores dotfiles for SYN-OS configuration
      ├──SYN-DOTFILES
      |
      # Scripts for the SYN-OS installer
      ├──SYN-INSTALLER-SCRIPTS
      |  ├──motd-primer.sh  # Script to set up the Message of the Day (MOTD) primer
      |  ├──motd.sh  # Script to set up the Message of the Day (MOTD)
      |  ├──syn-1_chroot.sh  # chroot script for SYN-OS
      |  ├──syn-ascii-art.sh  # Script that outputs SYN-OS ASCII art
      |  ├──syn-disk-variables.sh  # Disk variables for SYN-OS installer
      |  ├──syn-installer-functions.sh  # Functions for the SYN-OS installer
      |  ├──SYN-INSTALLER-MAIN.sh  # The main SYN-OS installer script
      |  └──syn-pacstrap-variables.sh  # Pacstrap variables for SYN-OS installer
      |
      # Configuration for the ISO profile of SYN-OS
      ├──SYN-ISO-PROFILE
      |  ├──airootfs  # Root filesystem for the archiso
      |  |  ├──etc  # System-wide configuration directory
      |  |  ├──root  # Root user home directory
      |  |  └──usr  # User system resources directory
      |  ├──bootstrap_packages.x86_64  # List of bootstrap packages for x86_64
      |  ├──efiboot  # EFI boot configuration
      |  ├──grub  # GRUB bootloader configuration
      |  ├──packages.x86_64  # List of packages for x86_64
      |  ├──pacman.conf  # Configuration file for Pacman
      |  ├──profiledef.sh  # Definition script for ISO profile
      |  └──syslinux  # Syslinux bootloader configuration
      |
      # Overlay for the root filesystem in SYN-OS
      ├──SYN-ROOTOVERLAY
      |  ├──boot  # Bootloader configurations and kernels
      |  └──etc  # System-wide configuration directory
      |
      # Tools for managing SYN-OS
      └──SYN-TOOLS
          ├──equip-profile-with-repo.sh  # Script to equip ISO profile with repo
          ├──REBUILD_ISO.sh  # Script to rebuild the ISO
          └──ShowInterfaceAddrLoop.sh  # Script to show interface addresses in a loop
    "
}

syn_directory_structure_extensive_description() {
    printf "
# The SYN-OS directory. This is the root of the SYN-OS project, which contains all the necessary resources and configuration files to run and manage SYN-OS.

SYN-OS
  |
  # This directory is for the fourth version of the SYN-OS project, it contains all necessary resources and configurations related to this specific version.
  └──SYN-OS-V4
      # The SYN-DOTFILES directory contains dotfiles used for setting up and configuring the environment in SYN-OS. Dotfiles are typically used to personalize your system.
      ├──SYN-DOTFILES
      |
      # This directory contains scripts used by the installer for SYN-OS. These scripts automate the process of installing and setting up SYN-OS on a system.
      ├──SYN-INSTALLER-SCRIPTS
      |  # Script to set up the Message of the Day (MOTD) primer. This script initializes the MOTD feature which presents useful information upon login to the terminal.
      |  ├──motd-primer.sh
      |  # Script to set up the Message of the Day (MOTD). This script configures the content of the MOTD.
      |  ├──motd.sh
      |  # chroot script for SYN-OS. This script is used to change the root directory for the current running process and its children. A typical use of a chroot is in an installation scenario where the installation environment is separate from the installed system.
      |  ├──syn-1_chroot.sh
      |  # Script that outputs SYN-OS ASCII art. It's a fun way to provide branding for the system in the terminal.
      |  ├──syn-ascii-art.sh
      |  # Disk variables for SYN-OS installer. These variables help determine where SYN-OS should be installed on the disk.
      |  ├──syn-disk-variables.sh
      |  # Functions for the SYN-OS installer. This script defines a number of functions that are used throughout the installer to perform various tasks.
      |  ├──syn-installer-functions.sh
      |  # The main SYN-OS installer script. This script coordinates the installation process and uses many of the other scripts in the SYN-INSTALLER-SCRIPTS directory.
      |  ├──SYN-INSTALLER-MAIN.sh
      |  # Pacstrap variables for SYN-OS installer. These variables are used by the Pacstrap tool to install the base package group of SYN-OS.
      |  └──syn-pacstrap-variables.sh
      |
      # The SYN-ISO-PROFILE directory contains configuration for the ISO profile of SYN-OS. This information is used when creating a bootable ISO image of SYN-OS.
      ├──SYN-ISO-PROFILE
      |  # The airootfs directory contains the root filesystem for the archiso. This includes all of the files that will be on the filesystem when the ISO is booted.
      |  ├──airootfs
      |  |  # The etc directory contains system-wide configuration files necessary for various system services and functions.
      |  |  ├──etc
      |  |  # The root directory is the home directory of the root user, who has full administrative rights on the system.
      |  |  ├──root
      |  |  # The usr directory is one of the most important directories in the system as it contains all the user binaries, their documentation, libraries, header files, etc.... In other words, all the most important data that the system uses when running.
      |  |  └──usr
      |  # A list of bootstrap packages for x86_64 architecture. These are the packages that are installed when you first boot the ISO.
      |  ├──bootstrap_packages.x86_64
      |  # EFI boot configuration directory. Contains necessary files for booting the system in EFI mode.
      |  ├──efiboot
      |  # GRUB bootloader configuration directory. Contains configuration files for GRUB, the bootloader used by SYN-OS.
      |  ├──grub
      |  # List of packages for x86_64 architecture. These are the packages that will be installed in the live environment of the ISO.
      |  ├──packages.x86_64
      |  # The pacman.conf file is the main configuration file for Pacman, the package manager used by SYN-OS.
      |  ├──pacman.conf
      |  # Definition script for ISO profile. This script contains a number of variables and settings that are used when creating the ISO.
      |  ├──profiledef.sh
      |  # Syslinux bootloader configuration directory. Contains configuration files for the Syslinux bootloader.
      |  └──syslinux
      |
      # The SYN-ROOTOVERLAY directory contains an overlay for the root filesystem in SYN-OS. This overlay is used to modify the files and structure of the root filesystem without changing the underlying filesystem.
      ├──SYN-ROOTOVERLAY
      |  # The boot directory contains bootloader configurations and kernel files. These are necessary for booting SYN-OS.
      |  ├──boot
      |  # The etc directory contains system-wide configuration files necessary for various system services and functions.
      |  └──etc
      |
      # The SYN-TOOLS directory contains tools for managing SYN-OS. These scripts perform various tasks related to managing and maintaining SYN-OS.
      └──SYN-TOOLS
          # Script to equip ISO profile with repository. This script configures the ISO to use a specific package repository.
          ├──equip-profile-with-repo.sh
          # Script to rebuild the ISO. This script automates the process of rebuilding the SYN-OS ISO with any changes made to the system or configuration.
          ├──REBUILD_ISO.sh
          # Script to show interface addresses in a loop. This script repeatedly outputs the IP addresses of the network interfaces on the system.
          └──ShowInterfaceAddrLoop.sh
    "
}
