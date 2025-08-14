#!/bin/zsh
# =============================================================================
#                               SYN-OS ISO Build Script
#       Automates the process of building a new ISO image for SYN-OS,
#       including setting up variables, configuring directories, using
#       mkarchiso, and cleaning up after the build.
# -----------------------------------------------------------------------------
#   Author: William Hayward-Holland (Syntax990)
#   License: MIT
# =============================================================================

# Root check
if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root."
    exit 1
fi

# Initialize color codes
RED='\e[1;31m'
GREEN='\e[1;32m'
YELLOW='\e[1;33m'
BLUE='\e[1;34m'
MAGENTA='\e[1;35m'
CYAN='\e[1;36m'
WHITE='\e[1;37m'
NC='\e[0m' # No Color

# Function to display build summary information
displayBuildSummary() {
    echo -e "${CYAN}================================================================================${NC}"
    echo -e "${GREEN}                      SYN-OS Installation Build Summary${NC}"
    echo -e "${CYAN}================================================================================${NC}\n"
    
    # Disk Configuration Summary
    echo -e "${YELLOW}Disk Configuration:${NC}"
    echo -e "${WHITE}WIPE_DISK_990:${NC} ${MAGENTA}/dev/sda${NC} - The disk that will be wiped and partitioned."
    echo -e "${WHITE}BOOT_PART_990:${NC} ${MAGENTA}/dev/sda1${NC} - Boot partition."
    echo -e "${WHITE}ROOT_PART_990:${NC} ${MAGENTA}/dev/sda2${NC} - Root partition."
    echo -e "${WHITE}BOOT_MOUNT_LOCATION_990:${NC} ${MAGENTA}/mnt/boot${NC} - Boot partition mount point."
    echo -e "${WHITE}ROOT_MOUNT_LOCATION_990:${NC} ${MAGENTA}/mnt${NC} - Root partition mount point."
    echo -e "${WHITE}BOOT_FILESYSTEM_990:${NC} ${MAGENTA}fat32${NC} - Filesystem for the boot partition."
    echo -e "${WHITE}ROOT_FILESYSTEM_990:${NC} ${MAGENTA}f2fs${NC} - Filesystem for the root partition.\n"
    
    # Package Arrays Summary
    echo -e "${YELLOW}Packages to be Installed:${NC}"
    
    # Base Packages
    echo -e "${GREEN}Base Packages:${NC}"
    basePackages=("base" "base-devel" "dosfstools" "fakeroot" "gcc" "linux" "linux-firmware" "archlinux-keyring" "pacman-contrib" "sudo" "zsh")
    echo -e "${WHITE}${(j: :)basePackages}${NC}\n"
    
    # System Packages
    echo -e "${GREEN}System Packages:${NC}"
    systemPackages=("alsa-utils" "archlinux-xdg-menu" "dhcpcd" "dnsmasq" "hostapd" "iwd" "pulseaudio" "python-pyalsa")
    echo -e "${WHITE}${(j: :)systemPackages}${NC}\n"
    
    # Control Packages
    echo -e "${GREEN}Control Packages:${NC}"
    controlPackages=("lxrandr" "obconf-qt" "pavucontrol-qt")
    echo -e "${WHITE}${(j: :)controlPackages}${NC}\n"
    
    # Window Manager Packages
    echo -e "${GREEN}Window Manager Packages:${NC}"
    wmPackages=("openbox" "qt5ct" "xcompmgr" "xorg-server" "xorg-xinit" "tint2")
    echo -e "${WHITE}${(j: :)wmPackages}${NC}\n"
    
    # CLI Packages
    echo -e "${GREEN}CLI Packages:${NC}"
    cliPackages=("git" "htop" "man" "nano" "reflector" "rsync" "wget")
    echo -e "${WHITE}${(j: :)cliPackages}${NC}\n"
    
    # GUI Packages
    echo -e "${GREEN}GUI Packages:${NC}"
    guiPackages=("engrampa" "feh" "kitty" "kwrite" "pcmanfm-qt")
    echo -e "${WHITE}${(j: :)guiPackages}${NC}\n"
    
    # Font Packages
    echo -e "${GREEN}Font Packages:${NC}"
    fontPackages=("terminus-font" "ttf-bitstream-vera")
    echo -e "${WHITE}${(j: :)fontPackages}${NC}\n"
    
    # CLI Extra Packages
    echo -e "${GREEN}CLI Extra Packages:${NC}"
    cliExtraPackages=("android-tools" "archiso" "binwalk" "brightnessctl" "hdparm" "hexedit" "lshw" "ranger" "sshfs" "yt-dlp")
    echo -e "${WHITE}${(j: :)cliExtraPackages}${NC}\n"
    
    # GUI Extra Packages
    echo -e "${GREEN}GUI Extra Packages:${NC}"
    guiExtraPackages=("audacity" "chromium" "gimp" "kdenlive" "obs-studio" "openra" "spectacle" "vlc")
    echo -e "${WHITE}${(j: :)guiExtraPackages}${NC}\n"
    
    # Bootloader Packages
    echo -e "${YELLOW}Bootloader Packages (Based on System Environment):${NC}"
    echo -e "${WHITE}If UEFI:${NC} ${MAGENTA}efibootmgr systemd${NC}"
    echo -e "${WHITE}If MBR:${NC} ${MAGENTA}syslinux${NC}\n"
    
    # Build Steps Summary
    echo -e "${YELLOW}Build Steps Overview:${NC}"
    echo -e "${CYAN}1. Environment Preparation:${NC} Configure keyboard layout, enable NTP, and start DHCP service."
    echo -e "${CYAN}2. Disk Partitioning and Formatting:${NC} Wipe ${MAGENTA}/dev/sda${NC}, create partitions, and format them."
    echo -e "${CYAN}   - For UEFI systems:${NC} Create GPT partition table, boot partition (${MAGENTA}/dev/sda1${NC}), and root partition (${MAGENTA}/dev/sda2${NC})."
    echo -e "${CYAN}   - For MBR systems:${NC} Create MS-DOS partition table and a single root partition."
    echo -e "${CYAN}3. Mount Partitions:${NC} Mount root partition to ${MAGENTA}/mnt${NC} and boot partition to ${MAGENTA}/mnt/boot${NC} (if UEFI)."
    echo -e "${CYAN}4. Pacstrap Sync:${NC} Install packages to the new system using pacstrap."
    echo -e "${CYAN}5. Generate fstab:${NC} Generate filesystem table with UUIDs."
    echo -e "${CYAN}6. Copy Dotfiles and Scripts:${NC} Copy configuration files and installation scripts to the new system."
    echo -e "${CYAN}7. Enter Chroot:${NC} Chroot into the new system and run ${MAGENTA}syn-stage1.zsh${NC} with environment variable ${MAGENTA}SYNOS_ENV${NC}.\n"
    
    echo -e "${CYAN}================================================================================${NC}"
    echo -e "${GREEN}                        End of SYN-OS Build Summary${NC}"
    echo -e "${CYAN}================================================================================${NC}\n"
}

# Function to check the success of the previous command
check_success() {
    if [ $? -ne 0 ]; then
        echo -e "${RED}$1${NC}"
        exit 1
    fi
}

# Display build summary and prompt user for confirmation
displayBuildSummary

echo -e "${RED}Warning:${NC} This script will perform operations that may modify existing directories and files."
echo -e "${RED}Ensure you have backed up important data.${NC}"
echo

# Prompt the user for confirmation using Zsh syntax
read "response?Do you want to proceed with building the SYN-OS ISO? (y/n): "

case "$response" in
    [yY]|[yY][eE][sS])
        echo "Proceeding with the build..."
        ;;
    [nN]|[nN][oO])
        echo "Exiting gracefully..."
        exit 0
        ;;
    *)
        echo "Invalid input. Please enter 'y' or 'n'. Exiting..."
        exit 1
        ;;
esac

# Function to create a new ISO image for SYN-OS
createIso() {
    ARCHISO_WORKDIR="/home/syntax990/GithubProjects/SYN-OS/WORKDIR"
    SYN_ISO_DIR="/home/syntax990/GithubProjects/SYN-OS/SYN-ISO-PROFILE"
    SYN_ISO_PROFILE="/home/syntax990/GithubProjects/SYN-OS/SYN-OS/SYN-ISO-PROFILE"

    # Remove existing directories if they exist
    [ -d "$ARCHISO_WORKDIR" ] && rm -rf "$ARCHISO_WORKDIR"
    [ -d "$SYN_ISO_DIR" ] && rm -rf "$SYN_ISO_DIR"

    # Create the ISO using mkarchiso
    mkarchiso -v -w "$ARCHISO_WORKDIR" -o "$SYN_ISO_DIR" "$SYN_ISO_PROFILE"
    check_success "Failed to create ISO using mkarchiso"
    
    echo -e "${GREEN}ISO creation completed successfully at ${SYN_ISO_DIR}${NC}"
}

createIso
