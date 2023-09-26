#!/bin/bash

# Check if the script is run as root
if [ "$(id -u)" -ne 0 ]; then
    printf "${RED}This script must be run as root${NC}\n"
    exit 1
fi


cp -vR /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-DOTFILES /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root
cp -vR /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-INSTALLER-SCRIPTS /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root
cp -vR /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ROOTOVERLAY /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root
