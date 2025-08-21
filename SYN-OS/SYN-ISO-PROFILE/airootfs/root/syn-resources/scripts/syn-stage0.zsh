#!/bin/zsh

# =============================================================================
#                             SYN-OS Stage 0 Script
#
# Purpose:
#   Stage 0 runs in the live environment (pre-chroot). It prepares disks,
#   mounts target filesystems, configures basic environment, installs packages
#   into the target root, and then chroots to Stage 1.
#
# Source layout and where to edit things:
#   - Disk vars are sourced from:
#       /root/syn-resources/scripts/syn-disk-config.zsh
#     Edit that file to change WIPE_DISK_990, partitions, mount points, FS types.
#
#   - Package arrays are sourced from:
#       /root/syn-resources/scripts/syn-packages.zsh
#     Edit that file to add or remove packages. Bootloader packages are appended
#     here based on firmware detection, not inside syn-packages.zsh.
#
# About the 990 suffix and helper functions:
#   - Variables using the 990 suffix are project-scoped settings intended to be
#     easy to grep and avoid collisions.
#   - Helper functions defined here are the canonical place for Stage 0 logic.
#     If you are looking for logic, prefer reading this script and Stage 1.
#
# Advisory:
#   Keep config files limited to simple assignments. Putting commands or logic
#   inside config files can cause side effects when sourced and may break flow.
#
# Meta:
#   SYN-OS      : The Syntax Operating System
#   Author      : William Hayward-Holland (Syntax990)
#   License     : MIT License
# =============================================================================

clear

# -----------------------------------------------------------------------------#
# Disk configuration
# -----------------------------------------------------------------------------#
DISK_CONFIG_FILE="/root/syn-resources/scripts/syn-disk-config.zsh"
if [ -f "$DISK_CONFIG_FILE" ]; then
    # shellcheck disable=SC1090
    source "$DISK_CONFIG_FILE"
else
    printf "\033[1;31mWarning: disk config not found at %s; using internal defaults.\033[0m\n" "$DISK_CONFIG_FILE"
    WIPE_DISK_990="/dev/vda"
    BOOT_PART_990="/dev/vda1"
    ROOT_PART_990="/dev/vda2"
    BOOT_MOUNT_LOCATION_990="/mnt/boot"
    ROOT_MOUNT_LOCATION_990="/mnt"
    BOOT_FILESYSTEM_990="fat32"
    ROOT_FILESYSTEM_990="f2fs"
fi

export WIPE_DISK_990 BOOT_PART_990 ROOT_PART_990 \
       BOOT_MOUNT_LOCATION_990 ROOT_MOUNT_LOCATION_990 \
       BOOT_FILESYSTEM_990 ROOT_FILESYSTEM_990

# -----------------------------------------------------------------------------#
# Firmware detection
# -----------------------------------------------------------------------------#
if [ -d "/sys/firmware/efi/efivars" ]; then
    SYNOS_ENV="UEFI"
    echo "Detected UEFI system."
else
    SYNOS_ENV="MBR"
    echo "Detected MBR (BIOS) system."
fi
export SYNOS_ENV

# -----------------------------------------------------------------------------#
# Packages
# -----------------------------------------------------------------------------#
# Package arrays: coreSystem, services, environmentShell, userApplications,
# developerTools, fontsLocalization, optionalFeatures, SYNSTALL
source /root/syn-resources/scripts/syn-packages.zsh

# -----------------------------------------------------------------------------#
# Helpers
# -----------------------------------------------------------------------------#
check_success() {
    if [ $? -ne 0 ]; then
        printf "\033[1;31mError: %s\033[0m\n" "$1"
        exit 1
    fi
}

format_boot_partition() {
    # Expect FAT32 for UEFI boot. Allow vfat naming too.
    case "$BOOT_FILESYSTEM_990" in
        fat32|vfat)
            mkfs.vfat -F 32 "$BOOT_PART_990"
            ;;
        *)
            printf "\033[1;33mWarning: BOOT_FILESYSTEM_990=%s not typical. Attempting mkfs via mkfs.%s\033[0m\n" "$BOOT_FILESYSTEM_990" "$BOOT_FILESYSTEM_990"
            "mkfs.$BOOT_FILESYSTEM_990" "$BOOT_PART_990"
            ;;
    esac
    check_success "Failed to format boot partition"
}

format_root_partition() {
    case "$ROOT_FILESYSTEM_990" in
        ext4)
            mkfs.ext4 -F "$ROOT_PART_990"
            ;;
        f2fs)
            mkfs.f2fs -f "$ROOT_PART_990"
            ;;
        btrfs)
            mkfs.btrfs -f "$ROOT_PART_990"
            ;;
        xfs)
            mkfs.xfs -f "$ROOT_PART_990"
            ;;
        *)
            printf "\033[1;33mWarning: ROOT_FILESYSTEM_990=%s not recognized. Attempting mkfs via mkfs.%s\033[0m\n" "$ROOT_FILESYSTEM_990" "$ROOT_FILESYSTEM_990"
            "mkfs.$ROOT_FILESYSTEM_990" "$ROOT_PART_990"
            ;;
    esac
    check_success "Failed to format root partition"
}

# -----------------------------------------------------------------------------#
# Aesthetics
# -----------------------------------------------------------------------------#
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
    echo "Without constraints; SYN-OS has independent freedom and creative intelligence."
    echo ""
    sleep 0.2
    clear
}

wipe_art_montage() {
    echo "\033[0;31m____    __    ____  __  .______    __  .__   __.   _______ \033[0m"
    echo "\033[0;31m\\   \\  /  \\  /   / |  | |   _  \\  |  | |  \\ |  |  /  _____|\033[0m"
    echo "\033[0;31m \\   \\/    \\/   /  |  | |  |_)  | |  | |   \\|  | |  |  __  \033[0m"
    echo "\033[0;31m  \\            /   |  | |   ___/  |  | |  .    | |  | |_ | \033[0m"
    echo "\033[0;31m   \\    /\\    /    |  | |  |      |  | |  |\\   | |  |__| | \033[0m"
    echo "\033[0;31m    \\__/  \\__/     |__| | _|      |__| |__| \\__|  \\______| \033[0m"
    echo ""
    echo "\033[1;31mIf you did not verify the target, you may wipe the wrong disk.\033[0m"
    echo "Press CTRL+C to abort."
    sleep 3
}

art_montage() {
    clear
    printf "\e[1;31m~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
    printf "     _______.____    ____ .__   __.          ______        _______.\n"
    printf "    /       |\\   \\  /   / |  \\ |  |         /  __  \\      /       |\n"
    printf "   |   (----  \\   \\/   /  |   \\|  |  ______|  |  |  |    |   (---- \n"
    printf "    \\   \\      \\_    _/   |  .    | |______|  |  |  |     \\   \\    \033[0m\n"
    printf "\033[0;31m.----)   |       |  |     |  |\\   |        |   --'  | .----)   |   \033[0m\n"
    printf "\033[0;31m|_______/        |__|     |__| \\__|         \\______/  |_______/    \033[0m\n"
    printf "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\e[0m\n\n"
    sleep 1
    echo "SYN-OS Stage 0 running in pre-chroot context."
    sleep 1
    clear
}

# -----------------------------------------------------------------------------#
# Environment prep
# -----------------------------------------------------------------------------#
syn_os_environment_prep() {
    echo "Setting keyboard layout to UK"
    loadkeys uk
    check_success "Failed to set keyboard layout"
#    timedatectl set-ntp true
#    check_success "Failed to set NTP"
#    echo "Starting DHCP client..."
#    systemctl start dhcpcd.service
#    check_success "Failed to start DHCP service"
}
# -----------------------------------------------------------------------------#
# Disk processing and mount
# -----------------------------------------------------------------------------#
disk_processing() {
    if [ "$SYNOS_ENV" = "UEFI" ]; then
        echo "Creating GPT and UEFI partitions on $WIPE_DISK_990..."
        parted --script "$WIPE_DISK_990" mklabel gpt \
            mkpart primary "$BOOT_FILESYSTEM_990" 1MiB 300MiB set 1 boot on
        check_success "Failed to create boot partition"
        parted --script "$WIPE_DISK_990" mkpart primary "$ROOT_FILESYSTEM_990" 301MiB 100%
        check_success "Failed to create root partition"

        echo "Formatting boot and root..."
        format_boot_partition
        format_root_partition
    else
        echo "Creating MBR and a single root partition on $WIPE_DISK_990..."
        parted --script "$WIPE_DISK_990" mklabel msdos \
            mkpart primary "$ROOT_FILESYSTEM_990" 1MiB 100%
        check_success "Failed to create MBR partition"

        echo "Formatting root..."
        # For MBR we formatted only the root partition; BOOT_PART_990 equals root here
        format_root_partition
    fi

    echo "Mounting root..."
    mount "$ROOT_PART_990" "$ROOT_MOUNT_LOCATION_990"
    check_success "Failed to mount root"

    if [ "$SYNOS_ENV" = "UEFI" ]; then
        echo "Mounting boot..."
        mkdir -p "$BOOT_MOUNT_LOCATION_990"
        mount "$BOOT_PART_990" "$BOOT_MOUNT_LOCATION_990"
        check_success "Failed to mount boot"
    fi
}

# -----------------------------------------------------------------------------#
# Pacstrap, mirrors, keyring, and bootloader selection
# -----------------------------------------------------------------------------#

pacstrap_sync() {
    # Refresh mirrors (requires reflector in the live env)
    echo "Refreshing mirrors..."
    reflector -c GB -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist

    # PGP keyring bootstrap (live env) so -K can copy it into the target
    echo "PGP keyring: initialise and populate"
    cat <<'EOF'
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢰⣶⣶⣶⣶⣶⣶⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⣶⣶⣶⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⣶⣶⣶⣶⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⣶⣶⡄⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⠿⠿⠿⠿⠿⠿⠿⠿⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠿⠿⠿⠿⠿⠿⠿⢿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⡿⠿⠀⠀⠀⠀⠀⠀⠀⠀⠸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⡇⠀⠀⠀⠀
⣀⣀⣀⣀⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣷⣶⣶⣶⡄⢸⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣷⣶⣶⣶⡆⢸⣿⣿⣿⣿⣧⣤⣤⣄⣀
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⡇⢸⣿⣿⣿⣿⣿⣿⣿⣿⠃⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⡇⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⡇⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⠿⠛⠛⠛⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⢿⣿⣿⡿⠛⠛⠛⠃⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⢰⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣶⣶⣶⣶⣶⣶⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣶⣶⣶⣶⣶⣶⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠿⠿⠿⠿⠿⠿⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠿⠿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡏⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⡏⠀⠀⠸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠇⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠸⣿⣿⣿⣿⣿
EOF
    pacman-key --init
    pacman-key --populate archlinux
    pacman -Sy
    sleep 1

    echo "Installing packages into target root..."
    # A little Pac-Man snack while pacstrap lines up packages
    cat <<'EOF'
⠀⠀⠀⠀⣀⣤⣴⣶⣶⣶⣦⣤⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⣠⣾⣿⣿⣿⣿⣿⣿⢿⣿⣿⣷⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀
⢀⣾⣿⣿⣿⣿⣿⣿⣿⣅⢀⣽⣿⣿⡿⠃⠀⠀⠀⠀⠀⠀⠀⠀
⣼⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠛⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠛⠁⠀⠀⣴⣶⡄⠀⣴⣶⡄⠀⣴⣶⡄
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣦⣀⠀⠙⠛⠁⠀⠙⠛⠁⠀⠙⠛⠁
⠸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣦⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠙⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠃⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠈⠙⠿⣿⣿⣿⣿⣿⣿⣿⠿⠋⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠉⠉⠉⠉⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
EOF
    sleep 1

    if [ "$SYNOS_ENV" = "UEFI" ]; then
        bootloaderPackages=(efibootmgr systemd)
    else
        bootloaderPackages=(syslinux)
    fi

    SYNSTALL+=("${bootloaderPackages[@]}")

    # -K copies the live keyring into the target so pacman works straight away
    pacstrap -K "$ROOT_MOUNT_LOCATION_990" "${SYNSTALL[@]}"
    check_success "Pacstrap failed"
}


# -----------------------------------------------------------------------------#
# Files into chroot
# -----------------------------------------------------------------------------#
dotfiles_and_vars() {
    echo "Generating fstab..."
    genfstab -U "$ROOT_MOUNT_LOCATION_990" >> "$ROOT_MOUNT_LOCATION_990/etc/fstab"
    check_success "genfstab failed"

    echo "Copying dotfiles overlay..."
    cp -Rv /root/syn-resources/DotfileOverlay/* "$ROOT_MOUNT_LOCATION_990"/
    check_success "Failed to copy dotfiles"

    echo "Copying Stage scripts and shared configs into target..."
    cp -v /root/syn-resources/scripts/syn-stage0.zsh "$ROOT_MOUNT_LOCATION_990/syn-stage0.zsh"
    check_success "Failed to copy stage0"

    cp -v /root/syn-resources/scripts/syn-stage1.zsh "$ROOT_MOUNT_LOCATION_990/syn-stage1.zsh"
    check_success "Failed to copy stage1"
    chmod +x "$ROOT_MOUNT_LOCATION_990/syn-stage1.zsh"

    cp -v /root/syn-resources/scripts/syn-packages.zsh "$ROOT_MOUNT_LOCATION_990/syn-packages.zsh"
    check_success "Failed to copy package config"

    # Ensure disk config is available inside chroot if Stage 1 sources it
    mkdir -p "$ROOT_MOUNT_LOCATION_990/root/syn-resources/scripts"
    cp -v /root/syn-resources/scripts/syn-disk-config.zsh "$ROOT_MOUNT_LOCATION_990/root/syn-resources/scripts/syn-disk-config.zsh"
    check_success "Failed to copy disk config"
}

# -----------------------------------------------------------------------------#
# Stage wrap-up visuals
# -----------------------------------------------------------------------------#
end_art() {
    clear
    echo ""
    printf "\033[32mSUMMARY: Stage 0 complete. Proceeding to Stage 1.\033[0m\n\n"
    printf "\033[32m• Root: %s mounted at %s\033[0m\n" "$ROOT_PART_990" "$ROOT_MOUNT_LOCATION_990"
    if [ "$SYNOS_ENV" = "UEFI" ]; then
        printf "\033[32m• Boot: %s mounted at %s (fs=%s)\033[0m\n" "$BOOT_PART_990" "$BOOT_MOUNT_LOCATION_990" "$BOOT_FILESYSTEM_990"
    fi
    printf "\033[32m• Root FS: %s\033[0m\n" "$ROOT_FILESYSTEM_990"
    printf "\033[32m• fstab generated, packages installed, scripts copied.\033[0m\n\n"
    sleep 2
}

# -----------------------------------------------------------------------------#
# Execution order
# -----------------------------------------------------------------------------#
syn_os_environment_prep
wipe_art_montage
disk_processing
face
art_montage
pacstrap_sync
face
dotfiles_and_vars
end_art

# -----------------------------------------------------------------------------#
# Enter chroot and run Stage 1
# -----------------------------------------------------------------------------#
echo "Entering chroot to execute Stage 1 with SYNOS_ENV=$SYNOS_ENV..."
arch-chroot "$ROOT_MOUNT_LOCATION_990" /bin/zsh -c "SYNOS_ENV=$SYNOS_ENV /syn-stage1.zsh"
check_success "Failed to execute Stage 1 inside chroot"
