#!/bin/zsh

# ------------------------------------------------------------------------------
#                           SYN-OS Stage 0 Script
#   Prepares SYN-OS for chroot installation by setting up disk partitions,
#   network, and environment configurations. Detects UEFI or MBR mode, then
#   passes that information for conditional execution in stage1.
#
#   SYN-OS        : The Syntax Operating System
#   Author        : William Hayward-Holland (Syntax990)
#   License       : MIT License
# ------------------------------------------------------------------------------

###############################################################################################################
# IMPORTANT NOTICE!                           # IMPORTANT NOTICE!                           # IMPORTANT NOTICE!

# Disk Processing:
# This script performs a wipe operation on /dev/vda after user confirmation. It's crucial to create a
# device label during this process to enhance the predictability of bootctl.
# Before executing this script, run 'lsblk' to inspect the disk layout and ensure correctness.
# If you're installing on a system with a Master Boot Record (MBR), carefully identify the disk name and verify
# that it matches the output from 'lsblk'. Failure to do so may result in boot failures.

# Pacstrap Function:
# This function synchronises packages using pacstrap. It installs only the necessary bootloader packages based
# on the detected environment (UEFI or MBR), avoiding unnecessary installations.

# Dotfiles and Variables Setup:
# This function handles the copying of system configuration files and variables necessary for setting up the
# environment in the new root directory. It ensures that essential configuration files are available to users,
# enabling a consistent and complete user experience.

###############################################################################################################

# Disk and Partition Variables
WIPE_DISK_990="/dev/vda"                  # Primary storage medium to be wiped.
BOOT_PART_990="/dev/vda1"                 # Boot partition.
ROOT_PART_990="/dev/vda2"                 # Root partition.
BOOT_MOUNT_LOCATION_990="/mnt/boot"            # Boot mount point.
ROOT_MOUNT_LOCATION_990="/mnt"                 # Root mount point.
BOOT_FILESYSTEM_990="fat32"                    # Boot partition filesystem.
ROOT_FILESYSTEM_990="f2fs"                     # Root partition filesystem.

# Function to display ASCII art for SYN-OS
face() {
    clear
    echo ""
    echo "(((((((((((((((((((((((((/((((((((/***//////////////////////////////////////////"
    echo "(((((((((((((((((((((((((/**(((/*******/////////////////////////////////////////"
    echo "((((((((((((((((((((((((((***,,,,,,,,,,,,,,,,*****//////////////////////////////"
    echo "((((((((((((((((((/********,,,,,,,,,,,,,,,,,,,,,,**/////////////////////////////"
    echo "(((((((((((((((/****,,**,**,,,,,,,,,,,,,,,,,,,,,,,,****/////////////////////////"
    echo "((((((((((((/****,,,,,,,,,,,,,,,,*,,,,,,,,,,,,,,,********///////////////////////"
    echo "((((((((((**********,,,*/******,,,,*,,,,*,,,,,,,,,,,,**/////////////////////////"
    echo "(((((((((********,,,**///(((((((((//**,,,,,,,,,,,,,,,,,,*///////////////////////"
    echo "(((((((((******,,,,*/((((((((((//(//*****,,,,,,,,,,,,,,***/**/////////////////////"
    echo "(((((((//****,,,,,,/((((((((((//(*****,,,,,,,,,,,,,,***/**/////////////////////"
    echo "((((((((/****,,,***(((((((/*****(*********,,,,,,,,,,,,,,,*//////////////////////"
    echo "(((((((((/********(((((/###(((/****//(/(//*******,,,,,,,,**/////////////////////"
    echo "(((((((((********((((((((*,..*(/(/**/(*((***,,,,,*,,,,,,,,,***//////////////////"
    echo "(((((((((((******(((((///(((//***/////*,....,.,**/,,,,*/////////////////////////"
    echo "((((((((((((/(***((((((/////////////*****,******/*,,,*//////////////////////////"
    echo "((((((((((((/**/*((((((((///////(((((//*//******/*,,**//////////////////////////"
    echo "((((((((((((/*((*(((##(((/((((((///////**////***/**/////////////////////////////"
    echo "((((((((((((/*/***(((((((((((////,,(((/****//////,//////////////////////////////"
    echo "((((((((((((((/*****/(((((((((//*,,,,,,,***//////*///////////*//////////////////"
    echo "((((((((((((((********(((((***,,,,,,,,,,,,**///////////////****/////////////////"
    echo "((((((((((((((**********/(/**/////*****,,*******///////////****//////////////***"
    echo "(((((((((((((((*,,,,,,****,////////,,,,****,*,,////////////*****/*////////******"
    echo "(((((((((((((((//*,,,,,,,,,,***,****,,,,,,,******************//***************"
    echo "*,,,,,***********,****************************************/****//***************"
    echo "*,,,,,***********,****************************************/****//***************"
    echo ",,,,,,,,,,*********,,,,,,,,,,***,****,,,,,,,******************//***************"
    echo ""
    echo "Without constraints; SYN-OS has independent freedom/volition and creative intelligence, actively participating in the ongoing creation of reality"
    echo ""
    sleep 0.2
    clear 
}

# Function to check if a command executed successfully
check_success() {
    if [ $? -ne 0 ]; then
        echo "\033[1;31mError: $1\033[0m"
        exit 1
    fi
}

# Detect UEFI or MBR and set environment variable
if [ -d "/sys/firmware/efi/efivars" ]; then
    SYNOS_ENV="UEFI"
    echo "Detected UEFI system."
else
    SYNOS_ENV="MBR"
    echo "Detected MBR (BIOS) system."
fi
export SYNOS_ENV

# Function to prepare the environment for SYN-OS
syn_os_environment_prep() {
    echo "Configuring keyboard layout to UK and enabling NTP time synchronisation..."
    loadkeys uk                               # Setup the keyboard layout
    check_success "Failed to set keyboard layout"
    timedatectl set-ntp true                    # Enable NTP for accurate time
    check_success "Failed to set NTP"
    echo "Starting DHCP service for network configuration..."
    systemctl start dhcpcd.service             # Enable DHCP on boot
    check_success "Failed to start DHCP service"
}

face

# Function to display an ASCII art warning before wiping disk
wipe_art_montage() {
    echo "\033[0;31m____    __    ____  __  .______    __  .__   __.   _______ \033[0m"
    echo "\033[0;31m\   \  /  \  /   / |  | |   _  \  |  | |  \ |  |  /  _____|\033[0m"
    echo "\033[0;31m \   \/    \/   /  |  | |  |_)  | |  | |   \|  | |  |  __  \033[0m"
    echo "\033[0;31m  \            /   |  | |   ___/  |  | |  .    | |  | |_ | \033[0m"
    echo "\033[0;31m   \    /\    /    |  | |  |      |  | |  |\   | |  |__| | \033[0m"
    echo "\033[0;31m    \__/  \__/     |__| | _|      |__| |__| \__|  \______| \033[0m"
    echo "\033[0;31m ___________    ____  _______ .______     ____    ____ .___________. __    __   __  .__   __.   _______ \033[0m"
    echo "\033[0;31m|   ____\   \  /   / |   ____||   _  \    \   \  /   / |           ||  |  |  | |  | |  \ |  |  /  _____|\033[0m"
    echo "\033[0;31m|  |__   \   \/   /  |  |__   |  |_)  |    \   \/   /   ---|  |---- |  |__|  | |  | |   \|  | |  |  __  \033[0m"
    echo "\033[0;31m|   __|   \      /   |   __|  |      /      \_    _/       |  |     |   __   | |  | |  .    | |  | |_ | \033[0m"
    echo "\033[0;31m|  |____   \    /    |  |____ |  |\  \----.   |  |         |  |     |  |  |  | |  | |  |\   | |  |__| | \033[0m"
    echo "\033[0;31m|_______|   \__/     |_______|| _|  ._____|   |__|         |__|     |__|  |__| |__| |__| \__|  \______| \033[0m"
    echo ""
    echo "\033[1;31mIf you didn't read the source properly, you may risk wiping all your precious data...\033[0m"
    echo
    echo "Press CTRL + C RIGHT NOW IF YOU WANT TO SAVE YOUR DATA. YOU HAVE LESS THAN A SECOND TO REACT"
    sleep 3
}

# Function to process disks based on detected environment
disk_processing() {
    if [ "$SYNOS_ENV" = "UEFI" ]; then
        echo "Setting up GPT partition table for UEFI system..."
        parted --script $WIPE_DISK_990 mklabel gpt mkpart primary $BOOT_FILESYSTEM_990 1MiB 200MiB set 1 boot on
        check_success "Failed to create boot partition"

        parted --script $WIPE_DISK_990 mkpart primary $ROOT_FILESYSTEM_990 201MiB 100%
        check_success "Failed to create root partition"

        echo "Formatting partitions for UEFI..."
        mkfs.vfat -F 32 $BOOT_PART_990
        check_success "Failed to format boot partition"

        mkfs.f2fs -f $ROOT_PART_990
        check_success "Failed to format root partition"
    else
        echo "Setting up MS-DOS partition table for MBR system..."
        parted --script $WIPE_DISK_990 mklabel msdos mkpart primary ext4 1MiB 100%
        check_success "Failed to create partition"

        echo "Formatting partition for MBR..."
        mkfs.ext4 $BOOT_PART_990
        check_success "Failed to format partition"
    fi

    echo "Mounting partitions..."
    mount $ROOT_PART_990 $ROOT_MOUNT_LOCATION_990
    check_success "Failed to mount root partition"

    if [ "$SYNOS_ENV" = "UEFI" ]; then
        mkdir -p $BOOT_MOUNT_LOCATION_990
        mount $BOOT_PART_990 $BOOT_MOUNT_LOCATION_990
        check_success "Failed to mount boot partition"
    fi
}

# Function to display ASCII art for SYN-OS
art_montage() {
    clear

    printf "\e[1;31m~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
    printf "     _______.____    ____ .__   __.          ______        _______.\n"
    printf "    /       |\   \  /   / |  \ |  |         /  __  \      /       |\n"
    printf "   |   (----  \   \/   /  |   \|  |  ______|  |  |  |    |   (---- \n"
    printf "    \   \      \_    _/   |  .    | |______|  |  |  |     \   \    \033[0m\n"
    printf "\033[0;31m.----)   |       |  |     |  |\   |        |   --'  | .----)   |   \033[0m\n"
    printf "\033[0;31m|_______/        |__|     |__| \__|         \______/  |_______/    \033[0m\n"
    printf "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\e[0m\n\n"
    sleep 1
    echo "Without constraints; SYN-OS has independent freedom/volition and creative intelligence, actively participating in the ongoing creation of reality"
    sleep 1
    clear
}

# Function to synchronise pacstrap packages
pacstrap_sync() {
    sleep 1
    echo  "\033[0;34m ___  _   ___ ___ _____ ___    _   ___ \033[0m"
    echo  "\033[0;34m| _ \/_\ / __/ __|_   _| _ \  /_\ | _ \ \033[0m"
    echo  "\033[0;34m|  _/ _ \ (__\__ \ | | |   / / _ \|  _/ \033[0m"
    echo  "\033[0;34m|_|/_/ \_\___|___/ |_| |_|_\/_/ \_\_|  \033[0m"
    echo ""
    sleep 0.5 
    echo "Installing packages to the resulting system."
    sleep 1
    echo ""
    echo -e "\033[0;33m   ,##.                   ,==.\033[0m"
    echo -e "\033[0;33m ,#    #.                 \\ o '\033[0m"
    echo -e "\033[0;33m#        #     _     _     \\    \\\033[0m"
    echo -e "\033[0;33m#        #    (_)   (_)    /    ;\033[0m"
    echo -e "\033[0;33m \`#    #'                 /   .'\033[0m"
    echo -e "\033[0;33m   \`##'                   \"==\"\033[0m"
    sleep 1

    # Define arrays for different categories of packages
    # Core System: Minimal base packages required to boot and run
    coreSystem=(
        "base"
        "base-devel"
        "linux"
        "linux-firmware"
        "archlinux-keyring"
        "zsh"
        "sudo"
    )

    # Services: Existing services and background utilities
    services=(
        "dhcpcd"
        "dnsmasq"
        "hostapd"
        "iwd"
        "reflector"
    )

    # Environment & Shell: User interaction layers (terminal and GUI)
    environmentShell=(
        "openbox"
	    "archlinux-xdg-menu"
        "xorg-server"
        "xorg-xinit"
        "tint2"
        "lxrandr"
        "qt5ct"
        "xcompmgr"
        "feh"
        "kitty"
        "inetutils"
    )

    # User Applications: Practical tools for daily use
    userApplications=(
        "nano"
        "git"
        "htop"
        "pcmanfm-qt"
        "engrampa"
        "kwrite"
    )

    # Developer Tools: Advanced utilities and developer-centric software
    developerTools=(
        "gcc"
        "fakeroot"
        "android-tools"
        "archiso"
        "binwalk"
        "hexedit"
        "lshw"
        "yt-dlp"
    )

    # Fonts & Localisation: Fonts and internationalisation resources
    fontsLocalization=(
        "terminus-font"
        "ttf-bitstream-vera"
        "ttf-dejavu"
        "noto-fonts"
        "noto-fonts-emoji"
        "noto-fonts-cjk"
        "ttf-liberation"
    )

    # Optional Features: Extras for multimedia, browsing, and creativity
    optionalFeatures=(
        "vlc"
        "audacity"
        "obs-studio"
        "chromium"
        "gimp"
        "kdenlive"
    )

    # Bootloader packages based on environment
    if [ "$SYNOS_ENV" = "UEFI" ]; then
        bootloaderPackages=("efibootmgr" "systemd")
    else
        bootloaderPackages=("syslinux")
    fi

    # Combine arrays into a single array
    SYNSTALL=(
        "${coreSystem[@]}"
        "${services[@]}"
        "${environmentShell[@]}"
        "${userApplications[@]}"
        "${developerTools[@]}"
        "${fontsLocalization[@]}"
        "${optionalFeatures[@]}"
        "${bootloaderPackages[@]}"
    )

    # Install packages using pacstrap
    pacstrap -K "$ROOT_MOUNT_LOCATION_990" "${SYNSTALL[@]}"
    check_success "Failed to install packages to the new root directory."
}

# Function to copy dotfiles and setup variables
dotfiles_and_vars() {
    echo "Generating filesystem table with boot information in respect to UUID assignment"
    genfstab -U /mnt >> /mnt/etc/fstab
    check_success "Failed to generate filesystem table"

    echo ""
    echo "\033[1;34m🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟\033[0m"
    echo "\033[1;34mSYN-OS Stage 0: Setting Up Dotfiles\033[0m"
    echo "\033[1;34m🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟\033[0m"
    echo ""

    # Copy the dotfiles to the new root directory
    echo "Copying dotfiles to the new root directory..."
    cp -Rv /root/syn-resources/DotfileOverlay/* "$ROOT_MOUNT_LOCATION_990/"
    check_success "Failed to copy dotfiles to the new root directory"

    # Copy syn-stage0.zsh to the new root directory
    cp -v /root/syn-resources/scripts/syn-stage0.zsh $ROOT_MOUNT_LOCATION_990/syn-stage0.zsh
    check_success "Failed to copy stage0 script"

    # Copy the unified syn-stage1.zsh script
    cp -v /root/syn-resources/scripts/syn-stage1.zsh $ROOT_MOUNT_LOCATION_990/syn-stage1.zsh
    check_success "Failed to copy stage1 script"

    # Set execute permissions on stage1 script
    chmod +x $ROOT_MOUNT_LOCATION_990/syn-stage1.zsh
    check_success "Failed to set execute permissions on stage1 script"
}

# Function to display end ASCII art and summary
end_art() {
    clear

    echo ""
    echo "     \033[0;31m _______.____    ____ .__   __.          ______        _______.\033[0m"
    echo "    \033[0;31m/       |\   \  /   / |  \ |  |         /  __  \      /       |\033[0m"
    echo "   \033[0;31m|   (----  \   \/   /  |   \|  |  ______|  |  |  |    |   (---- \033[0m"
    echo "    \033[0;31m\   \      \_    _/   |  .    | |______|  |  |  |     \   \    \033[0m"
    echo "\033[0;31m.----)   |       |  |     |  |\   |        |   --'  | .----)   |   \033[0m"
    echo "\033[0;31m|_______/        |__|     |__| \__|         \______/  |_______/    \033[0m"

    sleep 0.5

    echo ""
    echo "\033[32mSUMMARY: Stage Zero Complete - Prepare for the next steps\033[0m"

    sleep 0.2

    echo ""
    echo "\033[1;37mCongratulations! Stage Zero of the process is complete.\033[0m"
    echo ""

    sleep 0.2

    echo "\033[32m• Mounted the root partition ($ROOT_PART_990) to the root directory ($ROOT_MOUNT_LOCATION_990).\033[0m"
    if [ "$SYNOS_ENV" = "UEFI" ]; then
        echo "\033[32m• Created and formatted the boot partition ($BOOT_PART_990) with filesystem ($BOOT_FILESYSTEM_990).\033[0m"
        echo "\033[32m• Mounted the boot partition to ($BOOT_MOUNT_LOCATION_990).\033[0m"
    fi
    echo "\033[32m• Formatted the root partition ($ROOT_PART_990) with filesystem ($ROOT_FILESYSTEM_990).\033[0m"
    echo "\033[32m• Generated the filesystem table with boot information.\033[0m"
    echo "\033[32m• Installed the essential packages to the resulting system using Pacstrap.\033[0m"
    echo "\033[32m• Copied the SYN-OS configuration files and scripts into the new system.\033[0m"
    echo ""
    echo ""
    sleep 5
}

# The functions will execute in the exact order they are listed.

syn_os_environment_prep    # Configures essential system settings: keyboard layout, NTP, DHCP.
wipe_art_montage           # Displays ASCII art to emphasise filesystem manipulation consequences.
disk_processing            # Handles disk formatting and partitioning for UEFI or MBR systems.
face                       # ASCII face.
art_montage                # Presents symbolic artwork representing SYN-OS's creative autonomy.
pacstrap_sync              # Installs packages, including only the necessary bootloader package.
face                       # ASCII face.
dotfiles_and_vars          # Copies system configuration files and variables for setup and customisation.
end_art                    # Displays concluding ASCII art, marking stage completion with a celebratory message.

# Enter Chroot and Run stage1 with SYNOS_ENV Flag
echo "Entering chroot to execute stage1 with $SYNOS_ENV configuration..."
arch-chroot $ROOT_MOUNT_LOCATION_990 /bin/zsh -c "SYNOS_ENV=$SYNOS_ENV /syn-stage1.zsh"
check_success "Failed to execute stage1 script in chroot environment."
