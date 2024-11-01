# =============================================================================
#                                   SYN-OS .zshrc
#                Configuration and Setup for SYN-OS ISO Environment
# -----------------------------------------------------------------------------
#   This file contains environment variables, aliases, and settings 
#   necessary for the SYN-OS shell experience. 
#   Author: William Hayward-Holland (Syntax990)
#   License: M.I.T.
# =============================================================================

# Set environment language
export LANG=en_GB.UTF-8

# Set default text editor
export EDITOR='nano'

# Install script alias
alias syntax990="/root/syn-resources/scripts/syn-stage0.zsh"

# Display welcome message with system branding and instructions
clear
printf "\\n"
printf " ▄▄▄▄▄▄▄▄▄▄▄  ▄         ▄  ▄▄        ▄               ▄▄▄▄▄▄▄▄▄▄▄  ▄▄▄▄▄▄▄▄▄▄▄      \\n" 
printf "▐░░░░░░░░░░░▌▐░▌       ▐░▌▐░░▌      ▐░▌             ▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌     \\n"
printf "▐░█▀▀▀▀▀▀▀▀▀ ▐░▌       ▐░▌▐░▌░▌     ▐░▌             ▐░█▀▀▀▀▀▀▀█░▌▐░█▀▀▀▀▀▀▀▀▀      \\n"
printf "▐░▌          ▐░▌       ▐░▌▐░▌▐░▌    ▐░▌             ▐░▌       ▐░▌▐░▌               \\n"
printf "▐░█▄▄▄▄▄▄▄▄▄ ▐░█▄▄▄▄▄▄▄█░▌▐░▌ ▐░▌   ▐░▌ ▄▄▄▄▄▄▄▄▄▄▄ ▐░▌       ▐░▌▐░█▄▄▄▄▄▄▄▄▄      \\n"
printf "▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌▐░▌  ▐░▌  ▐░▌▐░░░░░░░░░░░▌▐░▌       ▐░▌▐░░░░░░░░░░░▌     \\n"
printf " ▀▀▀▀▀▀▀▀▀█░▌ ▀▀▀▀█░█▀▀▀▀ ▐░▌   ▐░▌ ▐░▌ ▀▀▀▀▀▀▀▀▀▀▀ ▐░▌       ▐░▌ ▀▀▀▀▀▀▀▀▀█░▌     \\n"
printf "          ▐░▌     ▐░▌     ▐░▌    ▐░▌▐░▌             ▐░▌       ▐░▌          ▐░▌     \\n"
printf " ▄▄▄▄▄▄▄▄▄█░▌     ▐░▌     ▐░▌     ▐░▐░▌             ▐░█▄▄▄▄▄▄▄█░▌ ▄▄▄▄▄▄▄▄▄█░▌     \\n"
printf "▐░░░░░░░░░░░▌     ▐░▌     ▐░▌      ▐░░▌             ▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌     \\n"
printf " ▀▀▀▀▀▀▀▀▀▀▀       ▀       ▀        ▀▀               ▀▀▀▀▀▀▀▀▀▀▀  ▀▀▀▀▀▀▀▀▀▀▀      \\n"
printf "\\n"
printf "Welcome to \033[38;2;23;147;209mSYN-OS\033[0m – Your customizable Arch Linux-based OS!\\n"
printf "\\n"
printf "Installation Guide:\\n"
printf "1. Plug in an Ethernet cable *or* connect to Wi-Fi using: \033[35miwctl\033[0m\\n"
printf "2. For mobile broadband (WWAN) modems, connect with: \033[35mmmcli\033[0m\\n"
printf "\\n"
printf "Start installation with the command: \033[38;2;23;147;209msyntax990\033[0m\\n"
printf "\\n"
printf "After installation, SYN-OS includes essential packages for a streamlined setup.\\n"
printf "\\n"
printf "To adjust default settings like hostname, username, or language:\\n"
printf "  - Edit \033[35m/root/syn-resources/scripts/syn-stage0.zsh\033[0m and \033[35msyn-stage1.zsh\033[0m before installing.\\n"
printf "    - To open these files, use: \033[35mnano /root/syn-resources/scripts/syn-stage0.zsh\033[0m\\n"
printf "      Modify variables to match your preferences.\\n"
printf "\\n"
printf "Want to build your own ISO with custom settings preloaded?\\n"
printf "  - Use \033[35mSYN-ISO-PROFILE\033[0m and \033[35mBUILD-SYNOS-ISO.zsh\033[0m from the repo on an existing Arch system to create your custom SYN-OS ISO.\\n"
printf "  - This allows you to skip editing files before each install!\\n"
printf "\\n"
printf "Creator: William Hayward-Holland (Syntax990)\\n"
printf "Support: [GitHub Repository](https://github.com/syn990/SYN-OS)\\n"
printf "Thank you for choosing \033[38;2;23;147;209mSYN-OS\033[0m!\\n"
