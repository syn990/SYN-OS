#!/bin/bash

# Check if the script is run as root
if [ "$(id -u)" -ne 0 ]; then
    printf "${RED}This script must be run as root${NC}\n"
    exit 1
fi

# Initialise colour codes
RED='\033[1;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m'

# Initialise variables
username="syntax990"
github_project_name="SYN-OS"
local_repo_name="SYN-OS-REPO"
version="SYN-OS-2035"
ARCHISO_PROFILE="SYN-ISO-PROFILE"

github_project_path="/home/$username/Github-Projects/$github_project_name"
local_repo_path="/home/$username/$local_repo_name"
releng_custom_path="/home/$username/Github-Projects/$github_project_name/$version/$ARCHISO_PROFILE"
cache_path="/var/cache/pacman/pkg"

# Check if the previous command was successful
check_success() {
    if [ $? -ne 0 ]; then
        printf "${RED}$1${NC}\n"
        exit 1
    fi
}

# Display information
display_info() {
    printf "${GREEN}Equipping local pacman repository to the Archiso profile...\n\n${NC}"
    sleep 0.5
    printf "Username: ${BLUE}$username${NC}\n"
    sleep 0.5
    printf "GitHub Project Name: ${BLUE}$github_project_name${NC}\n"
    sleep 0.5
    printf "Local Repository Path: ${BLUE}$local_repo_path${NC}\n"
    sleep 0.5
    printf "Cache path: ${BLUE}$cache_path${NC}\n"
    sleep 0.5
    printf "Releng custom path: ${BLUE}$releng_custom_path${NC}\n\n${NC}"
    sleep 0.5
}

# Clean up existing directories
clean_directories() {
    printf "${GREEN}Cleaning up existing directories...\n${NC}"
    rm -Rv $local_repo_path
    rm -Rv $releng_custom_path/airootfs/root/$local_repo_name
}

# Create a new directory for the local repository
create_repository() {
    printf "${GREEN}Creating a new directory for the local repository...\n${NC}"
    mkdir -p $local_repo_path
    check_success "Failed to create directory $local_repo_path"
}

# Copy packages from the local cache to the local repository
copy_packages() {
    printf "${GREEN}Copying all packages from the local cache to the local repository...\n${NC}"
    cp $cache_path/* $local_repo_path
    check_success "Failed to copy packages from $cache_path to $local_repo_path"
}

# Generate a new package database
generate_database() {
    printf "${GREEN}Generating a database for the local repository...\n${NC}"
    repo-add $local_repo_path/$local_repo_name.db.tar.gz $local_repo_path/*.pkg.tar.zst
    check_success "Failed to generate a database for the local repository"
}

# Copy the local repository to the Archiso releng profile
copy_to_releng() {
    printf "${GREEN}Copying the local repository $ARCHISO_PROFILE..\n${NC}"
    sleep 0.5

    # Create target directory if it doesn't exist
    mkdir -p $releng_custom_path/airootfs/root

    cp -rv $local_repo_path $releng_custom_path/airootfs/root
    check_success "Failed to copy the local repository to $..."
}


# Main function
main() {
    display_info
    check_success "Failed at display_info"

    clean_directories
#    check_success "Failed at clean_directories"

    create_repository
    check_success "Failed at create_repository"

    copy_packages
    check_success "Failed at copy_packages"

    generate_database
    check_success "Failed at generate_database"

    copy_to_releng
    check_success "Failed at copy_to_releng"

    printf "${GREEN}The script has completed its operations.\n${NC}"
}

main

