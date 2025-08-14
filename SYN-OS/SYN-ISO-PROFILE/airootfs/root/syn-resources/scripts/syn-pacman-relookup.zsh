#!/bin/zsh
# ------------------------------------------------------------------------------
#                         SYN-OS Pacman Relookup Script
#   This script refreshes the pacman keyring to resolve missing PGP signature errors.
#
#   SYN-OS        : The Syntax Operating System
#   Author        : William Hayward-Holland (Syntax990)
#   License       : MIT License
# ------------------------------------------------------------------------------
 
# Function to check if a command executed successfully
check_success() {
    if [ $? -ne 0 ]; then
        echo "\033[1;31mError: $1\033[0m" >&2
        exit 1
    fi
}

echo "Initializing pacman keyring..."
pacman-key --init
check_success "Failed to initialize pacman keyring."

echo "Populating default Arch Linux keys..."
pacman-key --populate archlinux
check_success "Failed to populate Arch Linux keys."

echo "Synchronizing package databases and updating archlinux-keyring..."
pacman -Sy archlinux-keyring
check_success "Failed to update archlinux-keyring."

echo "Refreshing pacman keys..."
pacman-key --refresh-keys
check_success "Failed to refresh pacman keys."

echo "Pacman keyring relookup complete."
