#!/bin/bash

# Function to check success
check_success() {
    if [ $? -ne 0 ]; then
        echo -e "\e[1;31m$1\e[0m"  # Print message in bold red color
        exit 1
    fi
}

confirm_wipe() {
	local message=$1
    local disclaimer="WARNING: This operation will permanently delete all data on the specified device. There is no way to recover the data once it is wiped. You are FULLY responsible for any consequences."

    echo -e "$disclaimer"
    read -p "Do you want to proceed with the data wipe? (y/n) " confirm
    if [ "$confirm" != "y" ]; then
        echo -e "Data wipe operation has been canceled."
        exit 0
    fi
}


check_if_arch_repo_are_accessible() {
    if ! ping -c 1 archlinux.org >/dev/null 2>&1; then
        echo "Error: Unable to reach Arch Linux repositories. Please check your network connection and try again."
        exit 1
    fi

    if ! host -t SOA archlinux.org >/dev/null 2>&1; then
        echo "Error: DNS resolution for Arch Linux repositories failed. Please check your DNS settings and try again."
        exit 1
    fi

    if ! curl -s --head https://archlinux.org >/dev/null 2>&1; then
        echo "Error: Unable to establish a secure connection to Arch Linux repositories. Please check your SSL/TLS configuration and try again."
        exit 1
    fi

    echo "Network connectivity is available. Proceeding."
}