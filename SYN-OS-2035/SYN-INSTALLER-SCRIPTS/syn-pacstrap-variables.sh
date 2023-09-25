#!/bin/bash

# This script installs packages using pacstrap for the initial setup of the main system.


# The following variables contain the names of packages to be installed.
# You can modify these variables to add or remove packages as needed.

basePackages="base base-devel dosfstools fakeroot gcc linux linux-firmware pacman-contrib sudo zsh"
systemPackages="alsa-utils archlinux-xdg-menu dhcpcd dnsmasq hostapd iwd pulseaudio python-pyalsa"
controlPackages="lxrandr obconf-qt pavucontrol-qt"
wmPackages="openbox xcompmgr xorg-server xorg-xinit tint2"
cliPackages="git htop man nano reflector rsync wget"
guiPackages="engrampa feh kitty kwrite pcmanfm-qt"
fontPackages="terminus-font ttf-bitstream-vera"
cliExtraPackages="android-tools archiso binwalk brightnessctl hdparm hexedit lshw ranger sshfs yt-dlp"
guiExtraPackages="audacity chromium gimp kdenlive obs-studio openra spectacle vlc"
#vmExtraPackages="edk2-ovmf libvirt qemu-desktop virt-manager virt-viewer"

# The SYNSTALL variable combines all the package variables for the pacstrap command.
SYNSTALL="$basePackages $systemPackages $controlPackages $wmPackages $cliPackages $guiPackages $fontPackages $cliExtraPackages $guiExtraPackages"

# Usage: pacstrap /mnt $SYNSTALL
# This command installs all the packages listed in the SYNSTALL variable to the specified mount point.
# Make sure the package names are valid and that the mirrors can be read.

pacstrap /mnt $SYNSTALL
