#!/bin/zsh

# Root check
if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root."
    exit 1
fi

# Initialise colour codes
RED='\033[0;34m'
GREEN='\033[0;32m'
BLUE='\033[1;94m'
NC='\033[0m'

# Function to check the success of the previous command
check_success() {
    if [ $? -ne 0 ]; then
        printf "${RED}$1${NC}\n"
        exit 1
    fi
}

    username="syntax990"
    github_project_name="SYN-OS"
    local_repo_name="SYN-OS-REPO"
    version="SYN-OS-2035"
    ARCHISO_PROFILE="SYN-ISO-PROFILE"

    github_project_path="/home/$username/Github-Projects/$github_project_name"
    local_repo_path="/root/$local_repo_name"
    releng_custom_path="/home/$username/Github-Projects/$github_project_name/$version/$ARCHISO_PROFILE"
    cache_path="/var/cache/pacman/pkg"

# Function to set up a local repository OF THE CURRENT SYSTEM YOU ARE USING
# Presumably Arch Linux. It will copy it's packages directly to the new Archiso profile
# Very Dangerous. May cause local conflicts and version control issues.
# Will 100% cause packages to not update in the correct order.
# Work can be done to ensure a robust local-pull is built.
setupLocalRepo() {    

    rm -Rv $local_repo_path
    rm -Rv $releng_custom_path/airootfs/root/$local_repo_name

    mkdir -p $local_repo_path
    check_success "Failed to create directory $local_repo_path"

    cp $cache_path/* $local_repo_path
    check_success "Failed to copy packages from $cache_path to $local_repo_path"

    repo-add $local_repo_path/$local_repo_name.db.tar.gz $local_repo_path/*.pkg.tar.zst
    check_success "Failed to generate a database for the local repository"

    mkdir -p $releng_custom_path/airootfs/root
    cp -rv $local_repo_path $releng_custom_path/airootfs/root
    check_success "Failed to copy the local repository to $releng_custom_path/airootfs/root"
}

# Function to copy dotfiles, the root overlay files and installer scripts from the GitHub repo directories to Archiso profile
copyDataToProfile() {
    # Check if the directories exist inside airootfs/root and delete them if present
    if [ -d "/home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root/SYN-DOTFILES" ] || [ -d "/home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root/SYN-INSTALLER-SCRIPTS" ] || [ -d "/home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root/SYN-ROOTOVERLAY" ]; then
        rm -rf /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root/SYN-DOTFILES
        rm -rf /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root/SYN-INSTALLER-SCRIPTS
        rm -rf /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root/SYN-ROOTOVERLAY
    fi

    # Copy dotfiles, installer scripts, and root overlay files to airootfs/root
    cp -vR /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-DOTFILES /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root
    cp -vR /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-INSTALLER-SCRIPTS /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root
    cp -vR /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ROOTOVERLAY /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root

    # Echo messages to the user
    echo "Files copied to airootfs/root successfully."

    # Check if the directories did not exist and notify the user
    if [ $? -eq 0 ]; then
        echo "The directories did not exist in airootfs/root, copying them now."
    fi
}

# Function to create a new ISO image for SYN-OS
createIso() {
    ARCHISO_WORKDIR="/home/syntax990/Github-Projects/SYN-OS/WORKDIR"
    SYN_ISO_DIR="/home/syntax990/Github-Projects/SYN-OS"
    SYN_ISO_PROFILE="/home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE"

    [ -d "$ARCHISO_WORKDIR" ] && { rm -R "$ARCHISO_WORKDIR"; }
    rm "$SYN_ISO_DIR"/*.iso

    mkarchiso -v -w "$ARCHISO_WORKDIR" -o "$SYN_ISO_DIR" "$SYN_ISO_PROFILE"
    check_success "Failed to create ISO using mkarchiso"
}

# Main menu function for interactive selection
menu() {
    clear
    echo "------------------------------------------------------------------"
    echo "${GREEN}SYN-OS Build and Deployment Interactive Prompt${NC}"
    echo "------------------------------------------------------------------"
    echo "1) Spin up a repository on-the-fly based on this system's packages:"
    echo
    echo "   - Pull data directly from this system's cache:  ${BLUE}[ ${NC}${RED}$cache_path${NC}${BLUE} ]${NC} Dangerous!"
    echo "   - Set up a Local Repository:                    ${BLUE}[ ${NC}${RED}$local_repo_path${NC}${BLUE} ]${NC}" 
    echo "   - Copy it to Archiso Profile:                   ${BLUE}[ ${NC}${RED}$releng_custom_path${NC}${BLUE} ]${NC}"
    echo 
    echo "2) Copy Data (Dotfiles, Root-overlay Materials, and Installer Scripts) to Archiso Profile"
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
    echo "3) Create New ISO Image for SYN-OS-2035 Archiso Project"
    echo "   - The ISO image will be based on the SYN-OS-2035 Archiso project configuration."
    echo 
    echo "4) Run all Steps in Sequence 1, 2, and 3 - Dangerous!"
    echo "   - ${BLUE}IMPORTANT:${NC} Press 4 unless you want to sequentially modify before building the ISO."
    echo
    echo "5) Quit"
    echo "------------------------------------------------------------------"
    echo
    echo "Please enter your choice: "

    read choice

    case $choice in
        1) 
            setupLocalRepo
            ;;
        2) 
            copyDataToProfile
            ;;
        3) 
            createIso
            ;;
        4)
            setupLocalRepo
            copyDataToProfile
            createIso
            ;;
        5) 
            exit 0
            ;;
        *) 
            echo "${RED}Invalid choice. Please try again.${NC}"
            ;;
    esac
}

# Infinite loop to keep the menu running
while true; do
    menu
    echo "Press [Enter] key to continue..."
    read dummy
done
