#!/bin/zsh

# This script is used to create a new ISO image for SYN-OS by performing a series of operations.
# It sets up the necessary variables, checks for existing directories and files, moves installer scripts,
# creates a new ISO using mkarchiso, and removes installer scripts from the target directory.
# Adapted from SYN-OS-V4 and SYN-OS-2035.

# Root check
if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root."
    exit 1
fi

# Function to create a new ISO image for SYN-OS
displayDotfileInformation()
{
    echo -e "${RED}Data${NC} (${NC}Dotfiles, Root-overlay Materials, and Installer Scripts${RED})${NC} ${RED}to Archiso Profile${NC}"
    echo
    echo "   - You can find all these in the profile >> ${GREEN}(/airootfs/root/syn-resources/DotfileOverlay/etc/skel)${NC} or on the SYN-LIVE >> ${GREEN}(/root/syn-resources/DotfileOverlay/etc/skel)${NC} ) "
    echo
    echo "   - ${BLUE}autostart:${NC} Stores the 'lxrandr-autostart.desktop' graphical resolution setting."
    echo "   - ${BLUE}dconf:${NC} Contains 'user' settings for the Dconf database, a low-level configuration system."
    echo "   - ${BLUE}htop:${NC} Houses 'htoprc', which holds user-defined settings for the htop utility."
    echo "   - ${BLUE}kitty:${NC} Contains 'kitty.conf', a configuration file for the Kitty terminal emulator."
    echo "   - ${BLUE}openbox:${NC} Holds multiple files for Openbox window manager and desktop panel configurations."
    echo "   - ${BLUE}pavucontrol-qt:${NC} Includes 'pavucontrol-qt.conf' for configuring the QT-based PulseAudio mixer."
    echo "   - ${BLUE}pcmanfm-qt/default:${NC} Contains 'recent-files.conf' and 'settings.conf' for PcmanFM-Qt, a file manager."
    echo "   - ${BLUE}pulse:${NC} Manages PulseAudio settings including databases and default sink/source configurations."
    echo "   - ${BLUE}qt5ct/colors:${NC} Houses 'syntax990.conf' for QT-based applications. These are the SYN-OS QT window colors."
    echo "   - ${BLUE}ranger:${NC} Consists of multiple configuration files for the Ranger file manager."
    echo "   - ${BLUE}tint2:${NC} Includes a variety of tint2 configurations, this is the panel. Themes like 'SYN-RTOS-DARKRED_TOP.tint2rc' are included."
    echo "   - ${BLUE}vlc:${NC} Contains 'vlc-qt-interface.conf' and 'vlcrc' for VLC media player configurations."
    echo
    echo

}

# Function to check the success of the previous command
check_success() {
    if [ $? -ne 0 ]; then
        printf "${RED}$1${NC}\n"
        exit 1
    fi
}

# Initialise colour codes
RED='\033[0;34m'
GREEN='\033[0;32m'
BLUE='\033[1;94m'
NC='\033[0m'

displayDotfileInformation

echo -e "${RED}Warning:${NC} This script will perform operations that may modify existing directories and files."
echo -e "${RED}Ensure you have backed up important data.${NC} Press Y to proceed, or press N or Ctrl+C to cancel."
read -k1 -s response

case $response in
    [yY])
        echo "Proceeding..."
        ;;
    [nN])
        echo "Exiting Gracefully (You are paying attention)..."
        exit 0
        ;;
    *)
        echo "Invalid input. You are not paying attention. Exiting..."
        exit 1
        ;;
esac


# Function to create a new ISO image for SYN-OS
createIso() {
    ARCHISO_WORKDIR="/home/syntax990/SYN-OS/WORKDIR"
    SYN_ISO_DIR="/home/syntax990/SYN-OS/SYN-ISO-PROFILE"
    SYN_ISO_PROFILE="/home/syntax990/SYN-OS/SYN-OS/SYN-OS/SYN-ISO-PROFILE"

    [ -d "$ARCHISO_WORKDIR" ] && { rm -R "$ARCHISO_WORKDIR"; }
    [ -d "$SYN_ISO_DIR" ] && { rm -R "$SYN_ISO_DIR"; }

    mkarchiso -v -w "$ARCHISO_WORKDIR" -o "$SYN_ISO_DIR" "$SYN_ISO_PROFILE"
    check_success "${RED}Failed to create ISO using mkarchiso${NC}"
}

createIso
