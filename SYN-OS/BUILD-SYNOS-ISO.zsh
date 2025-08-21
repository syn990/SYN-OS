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

# ------------------------------------------------------------------------------
# Import central package definitions and ISO package list
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PACKAGES_FILE="$SCRIPT_DIR/SYN-ISO-PROFILE/airootfs/root/syn-resources/scripts/syn-packages.zsh"
if [ -f "$PACKAGES_FILE" ]; then
    source "$PACKAGES_FILE"
else
    echo -e "${RED}Error: Could not find package definitions at $PACKAGES_FILE${NC}"
    exit 1
fi

# Load disk configuration from the same location used by the installer. This file defines
# WIPE_DISK_990, BOOT_PART_990, ROOT_PART_990, BOOT_MOUNT_LOCATION_990,
# ROOT_MOUNT_LOCATION_990, BOOT_FILESYSTEM_990 and ROOT_FILESYSTEM_990. By
# sourcing this file here we ensure that the build summary reflects any
# customisations the user has made to the disk layout. If the file is not
# found, reasonable defaults will be set within it.
DISK_CONFIG_FILE="$SCRIPT_DIR/SYN-ISO-PROFILE/airootfs/root/syn-resources/scripts/syn-disk-config.zsh"
if [ -f "$DISK_CONFIG_FILE" ]; then
    # shellcheck disable=SC1090
    source "$DISK_CONFIG_FILE"
fi
ISO_LIST_FILE="$SCRIPT_DIR/SYN-ISO-PROFILE/packages.x86_64"
isoPackages=()
if [ -f "$ISO_LIST_FILE" ]; then
    while IFS= read -r pkg; do
        # skip empty lines and comments
        [[ -z "$pkg" || "$pkg" == \#* ]] && continue
        isoPackages+=("$pkg")
    done < "$ISO_LIST_FILE"
fi

# Function to display build summary information
displayBuildSummary() {
    echo -e "${CYAN}================================================================================${NC}"
    echo -e "${GREEN}                      SYN-OS Installation Build Summary${NC}"
    echo -e "${CYAN}================================================================================${NC}\n"
    
    # Disk Configuration Summary
    echo -e "${YELLOW}Disk Configuration:${NC}"
    echo -e "${WHITE}WIPE_DISK_990:${NC} ${MAGENTA}${WIPE_DISK_990}${NC} - The disk that will be wiped and partitioned."
    echo -e "${WHITE}BOOT_PART_990:${NC} ${MAGENTA}${BOOT_PART_990}${NC} - Boot partition."
    echo -e "${WHITE}ROOT_PART_990:${NC} ${MAGENTA}${ROOT_PART_990}${NC} - Root partition."
    echo -e "${WHITE}BOOT_MOUNT_LOCATION_990:${NC} ${MAGENTA}${BOOT_MOUNT_LOCATION_990}${NC} - Boot partition mount point."
    echo -e "${WHITE}ROOT_MOUNT_LOCATION_990:${NC} ${MAGENTA}${ROOT_MOUNT_LOCATION_990}${NC} - Root partition mount point."
    echo -e "${WHITE}BOOT_FILESYSTEM_990:${NC} ${MAGENTA}${BOOT_FILESYSTEM_990}${NC} - Filesystem for the boot partition."
    echo -e "${WHITE}ROOT_FILESYSTEM_990:${NC} ${MAGENTA}${ROOT_FILESYSTEM_990}${NC} - Filesystem for the root partition.\n"
    
    # Packages summary sourced from syn-packages.zsh
    echo -e "${YELLOW}Packages to be Installed:${NC}"

    # Display package categories defined in syn-packages.zsh
    echo -e "${GREEN}Core System:${NC}"
    echo -e "${WHITE}${(j: :)coreSystem}${NC}\n"

    echo -e "${GREEN}Services:${NC}"
    echo -e "${WHITE}${(j: :)services}${NC}\n"

    echo -e "${GREEN}Environment & Shell:${NC}"
    echo -e "${WHITE}${(j: :)environmentShell}${NC}\n"

    echo -e "${GREEN}User Applications:${NC}"
    echo -e "${WHITE}${(j: :)userApplications}${NC}\n"

    echo -e "${GREEN}Developer Tools:${NC}"
    echo -e "${WHITE}${(j: :)developerTools}${NC}\n"

    echo -e "${GREEN}Fonts & Localisation:${NC}"
    echo -e "${WHITE}${(j: :)fontsLocalization}${NC}\n"

    echo -e "${GREEN}Optional Features:${NC}"
    echo -e "${WHITE}${(j: :)optionalFeatures}${NC}\n"

    echo -e "${GREEN}ISO Packages (live environment only):${NC}"
    if (( ${#isoPackages[@]} )); then
        echo -e "${WHITE}${(j: :)isoPackages}${NC}\n"
    else
        echo -e "${WHITE}No ISO-specific packages found${NC}\n"
    fi
    
    # Bootloader Packages
    echo -e "${YELLOW}Bootloader Packages (Based on System Environment):${NC}"
    echo -e "${WHITE}If UEFI:${NC} ${MAGENTA}efibootmgr systemd${NC}"
    echo -e "${WHITE}If MBR:${NC} ${MAGENTA}syslinux${NC}\n"
    
    # Build Steps Summary
    echo -e "${YELLOW}Build Steps Overview:${NC}"
    echo -e "${CYAN}1. Environment Preparation:${NC} Configure keyboard layout, enable NTP, and start DHCP service."
    echo -e "${CYAN}2. Disk Partitioning and Formatting:${NC} Wipe ${MAGENTA}${WIPE_DISK_990}${NC}, create partitions, and format them."
    echo -e "${CYAN}   - For UEFI systems:${NC} Create GPT partition table, boot partition (${MAGENTA}${BOOT_PART_990}${NC}), and root partition (${MAGENTA}${ROOT_PART_990}${NC})."
    echo -e "${CYAN}   - For MBR systems:${NC} Create MS-DOS partition table and a single root partition."
    echo -e "${CYAN}3. Mount Partitions:${NC} Mount root partition to ${MAGENTA}${ROOT_MOUNT_LOCATION_990}${NC} and boot partition to ${MAGENTA}${BOOT_MOUNT_LOCATION_990}${NC} (if UEFI)."
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
