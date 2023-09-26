#!/bin/bash

# Import additional functions
source /root/SYN-INSTALLER-SCRIPTS/syn-installer-functions.sh

# ANSI Colour Codes
RED="\033[1;31m"
GREEN="\033[1;32m"
YELLOW="\033[1;33m"
BLUE="\033[1;34m"
RESET="\033[0m"

clear

# Display SYN-OS ASCII Art
echo "${GREEN}"
echo "01010011 01011001 01001110 00101101 01001111 01010011"
echo "SYN-OS: The Syntax Operating System"
echo "${RESET}"

# Welcome message
printf "${YELLOW}Welcome to SYN-OS.${RESET}\\n"
printf "This system is built on the robust architecture of Arch Linux.\\n"

# Context of the script
printf "\\n${BLUE}You are currently in the SYN-OS installation environment.${RESET}\\n"
printf "The SYN-INSTALLER-MAIN.sh script has guided the system setup and initialisation.\\n"
printf "Execute that script to begin the installation.\\n"

# Status and Tips
printf "\\n${RED}Status:${RESET}\\n"

check_if_arch_repo_are_accessible

# Next steps
printf "\\n${YELLOW}Next Steps:${RESET}\\n"
printf "1. Use ${GREEN}arch-chroot${RESET} to enter the new environment.\\n"
printf "2. Run additional post-installation scripts as needed.\\n"
printf "3. Review logs and configurations before rebooting.\\n"

# Support and resources
printf "\\n${BLUE}Support and Resources:${RESET}\\n"
printf "• For installation queries, contact ${YELLOW}Syntax990${RESET} via email at ${BLUE}william@npc.syntax990.com${RESET}\\n"
printf "• Source code and build materials can be accessed on GitHub: ${BLUE}https://github.com/syn990/syn-rtos.git${RESET}\\n"
printf "${RED}Exercise caution whilst installing and configuring. Refer to the GitHub repository for documentation.${RESET}\\n\\n"

