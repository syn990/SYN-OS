#!/bin/bash

# Set the color codes
RED='\033[1;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Check if the previous command was successful
check_success() {
    if [ $? -ne 0 ]; then
        printf "${RED}$1${NC}\n"
        exit 1
    fi
}

# Variables
username="syntax990"                                                # Username for the current system
git_repository_name="SYN-OS"                                            # Name of the new local repository
pacman_repository_name="SYN-OS-REPO"
profile_name="SYN-OS-V4"                                            # Name of the Archiso profile

repository_path="/home/$username/$git_repository_name"                  # Path to the new local repository
releng_custom_path="/home/$username/$git_repository_name/$profilename"  # Path to the custom Archiso profile


cache_path="/var/cache/pacman/pkg"                                  # Path to the local Pacman cache


printf "${GREEN}Equipping local pacman repository to the Archiso profile...\n\n"
sleep 0.5
printf "Username: ${BLUE}$username${NC}\n"
sleep 0.5
printf "Repository name: ${BLUE}$git_repository_name${NC}\n"
sleep 0.5
printf "Repository path: ${BLUE}$repository_path${NC}\n"
sleep 0.5
printf "Cache path: ${BLUE}$cache_path${NC}\n"
sleep 0.5
printf "Releng custom path: ${BLUE}$releng_custom_path${NC}\n\n${NC}"
sleep 0.5


# Clean up existing directories to ensure a fresh start
printf "${GREEN}Cleaning up existing directories...\n${NC}"
sleep 0.5
rm -Rv $repository_path
rm -Rv $releng_custom_path/airootfs/root/$pacman_repository_name

# Create a new directory for the repository
printf "${GREEN}Creating a new directory for the repository...\n${NC}"
sleep 0.5
mkdir -p $repository_path
check_success "Failed to create directory $repository_path"

# Copy all packages from the local Pacman cache to the new repository
printf "${GREEN}Copying all packages from the local Pacman cache to the new repository...\n${NC}"
sleep 0.5
cp $cache_path/* $repository_path
check_success "Failed to copy packages from $cache_path to $repository_path"

# Generate a database for the new repository to manage packages
printf "${GREEN}Generating a database for the new repository to manage packages...\n${NC}"
sleep 0.5
repo-add $repository_path/$git_repository_name.db.tar.gz $repository_path/*.pkg.tar.zst
check_success "Failed to generate a database for the new repository"

# Copy the repository directory to the releng profile in Archiso
printf "${GREEN}Copying the repository directory to the releng profile in Archiso...\n${NC}"
sleep 0.5
cp -rv $repository_path $releng_custom_path/airootfs/root
check_success "Failed to copy the repository directory to the releng profile in Archiso"

printf "${RED}The script has completed its operations.\n${NC}"
