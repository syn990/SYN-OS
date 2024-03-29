#!/bin/bash

# Source additional functions
source /root/SYN-OS-2035/SYN-INSTALLER-SCRIPTS/syn-installer-functions.sh

# ANSI Colour Codes
RED="\033[1;31m"
GREEN="\033[1;32m"
YELLOW="\033[1;33m"
BLUE="\033[1;34m"
RESET="\033[0m"

clear

# Display SYN-OS ASCII Art
echo "${GREEN}"
cat << "EOF"
01010011 01011001 01001110 00101101 01001111 01010011
SYN-OS: The Syntax Operating System

EOF
echo "${RESET}"

# Welcome Message
printf "${YELLOW}Welcome to SYN-OS.${RESET}\n"
printf "This system is built on the robust architecture of Arch Linux.\n"

# Script Context
printf "\n${BLUE}You are currently in the SYN-OS installation environment.${RESET}\n"
printf "The SYN-INSTALLER-MAIN.sh script has guided the system setup and initialisation.\n"
printf "Execute that script to begin installation.\n"

# Status and Tips
printf "\n${RED}Status:${RESET}\n"

# Check network connectivity to Arch Linux repositories
check_if_arch_repo_are_accessible

if [ $? -eq 0 ]; then
    printf "• Arch Linux repositories accessible: ${GREEN}Online${RESET}\n"
else
    printf "• Arch Linux repositories accessible: ${RED}Offline${RESET}\n"
fi

printf "• SYN-OS components loaded: ${GREEN}Successful${RESET}\n"

# What to do next
printf "\n${YELLOW}Next Steps:${RESET}\n"
printf "1. Use ${GREEN}arch-chroot${RESET} to enter the new environment.\n"
printf "2. Run additional post-installation scripts as needed.\n"
printf "3. Check logs and configurations before rebooting.\n"

# Footer
printf "\n${BLUE}Support and Resources:${RESET}\n"
printf "• For installation enquiries, reach out to ${YELLOW}Syntax990${RESET} via email at ${BLUE}william@npc.syntax990.com${RESET}\n"
printf "• Source code and build materials can be accessed on GitHub: ${BLUE}https://github.com/syn990/syn-rtos.git${RESET}\n"
printf "${RED}Exercise caution while installing and configuring. Refer to the GitHub repository for documentation.${RESET}\n\n"
