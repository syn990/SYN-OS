#!/bin/bash

# Function to check success
check_success() {
    if [ $? -ne 0 ]; then
        echo -e "\e[1;31m$1\e[0m"  # Print message in bold red color
        exit 1
    fi
}

# Function to display a warning message and confirm the action
confirm_action() {
    local message=$1
    local disclaimer=$'\n\n\e[1;33mWARNING:\e[0m By proceeding, you acknowledge and agree that William (Syntax990) assumes \e[1;33mNO responsibility\e[0m for ANY outcomes. \e[1;33mABSOLUTELY NO WARRANTIES\e[0m are provided. You are \e[1;33mFULLY accountable\e[0m for the consequences.'

    echo -e "$disclaimer\n$message"
    read -p "Do you want to proceed? (y/n) " confirm
    if [ "$confirm" != "y" ]; then
        echo -e "\e[1;31mAutonomous judgment waiver execution has been halted. It is IMPERATIVE to comply with all tenets of the RTFM act!! MODIFY syn-stage0.sh immediately to ensure system viability and PREVENT IRREVOCABLE DATA DESTRUCTION!\e[0m"
        exit 0
    fi
}


check_if_arch_repo_are_accessible() {
    ping -c 1 archlinux.org >/dev/null 2>&1
    if [ $? -eq 0 ]; then
        echo "Network connectivity is available. Proceeding."
    else
        echo "Error: Unable to reach Arch Linux repositories. Please check your network connection and try again."
        exit 1
    fi
}
