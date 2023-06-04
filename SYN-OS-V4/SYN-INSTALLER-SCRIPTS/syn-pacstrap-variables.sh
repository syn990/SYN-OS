#!/bin/bash

# Below you will find all the packages used on the initial pacstrap before installing files to the main system.
# You can insert package names found in the repositories here if you want them installed on the initial pacstrap.
# Or simply scroll down to the pacstrap and add additional packages: "pacstrap /mnt $SYNSTALL *package_name_here*"

# You can add/remove packages in these variables. It's done this way so you can see what's being installed.
# This implementation means you can modify the script and even omit entire sections conveniently.

# All packages are installed in a single pacstrap command, allowing a total-size prediction for all packages during install.
# Ensure the package name is valid, and the mirrors can be read, and pacstrap will install it.


BASE_990="base base-devel dosfstools fakeroot gcc linux linux-firmware pacman-contrib sudo zsh"
SYSTEM_990__="alsa-utils archlinux-xdg-menu dhcpcd dnsmasq hostapd iwd pulseaudio python-pyalsa"
CONTROL_990_="lxrandr obconf-qt pavucontrol-qt"
WM_990______="openbox xcompmgr xorg-server xorg-xinit tint2"
CLI_990_____="git htop man nano reflector rsync wget"
GUI_990_____="engrampa feh kitty kwrite pcmanfm-qt"
FONT_990____="terminus-font ttf-bitstream-vera"
CLI_XTRA_990="android-tools archiso binwalk brightnessctl hdparm hexedit lshw ranger sshfs yt-dlp"
GUI_XTRA_990="audacity chromium gimp kdenlive obs-studio openra spectacle vlc"
#VM_XTRA_990_="edk2-ovmf libvirt qemu-desktop virt-manager virt-viewer"

SYNSTALL="$BASE_990 $SYSTEM_990__ $CONTROL_990_ $WM_990______ $CLI_990_____ $GUI_990_____ $FONT_990____ $CLI_XTRA_990 $GUI_XTRA_990 $VM_XTRA_990_"
