#!/bin/zsh

# Root check
if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root."
    exit 1
fi

# Initialise colour codes
RED='\033[1;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

# Function to check the success of the previous command
check_success() {
    if [ $? -ne 0 ]; then
        printf "${RED}$1${NC}\n"
        exit 1
    fi
}

# Function to set up a local repository OF THE CURRENT SYSTEM and copy it to Archiso profile
setupLocalRepo() {    
    username="syntax990"
    github_project_name="SYN-OS"
    local_repo_name="SYN-OS-REPO"
    version="SYN-OS-2035"
    ARCHISO_PROFILE="SYN-ISO-PROFILE"

    github_project_path="/home/$username/Github-Projects/$github_project_name"
    local_repo_path="/root/$local_repo_name"
    releng_custom_path="/home/$username/Github-Projects/$github_project_name/$version/$ARCHISO_PROFILE"
    cache_path="/var/cache/pacman/pkg"

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

# Function to copy dotfiles, the root overlay files and installer scripts from the GitHub repo directories to Archiso profile
# Function to copy dotfiles, the root overlay files, and installer scripts from the GitHub repo directories to Archiso profile
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


# Main menu function for interactive selection
menu() {
    clear
    echo "------------------------------------------------------------------"
    echo "${GREEN}SYN-OS Build and Deployment Interactive Prompt${NC}"
    echo "------------------------------------------------------------------"
    echo "1) Setup Local Repository and Copy to Archiso Profile"
    echo "2) Copy Data (The dotfiles, root-overlay materials and the installer scripts) to Archiso Profile"
    echo "3) Create New ISO Image for SYN-OS"
    echo "4) Run all steps in sequence 1, 2 and 3"
    echo "5) Quit"
    echo "------------------------------------------------------------------"
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
