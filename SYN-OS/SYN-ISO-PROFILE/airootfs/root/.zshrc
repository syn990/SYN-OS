# =============================================================================
#                             SYN-OS .zshrc
#       Live ISO Environment Configuration & Installation Splash Screen
# -----------------------------------------------------------------------------
#   This file provides system information, installation instructions, and
#   customization options upon booting into the SYN-OS live environment.
#   Author: William Hayward-Holland (Syntax990)
#   License: MIT
# =============================================================================

# Clear terminal before displaying splash screen
clear

# =============================================================================
# SYN-OS ASCII Branding
# =============================================================================
cat << "EOF"
 ▄▄▄▄▄▄▄▄▄▄▄  ▄         ▄  ▄▄        ▄               ▄▄▄▄▄▄▄▄▄▄▄  ▄▄▄▄▄▄▄▄▄▄▄
▐░░░░░░░░░░░▌▐░▌       ▐░▌▐░░▌      ▐░▌             ▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌
▐░█▀▀▀▀▀▀▀▀▀ ▐░▌       ▐░▌▐░▌░▌     ▐░▌             ▐░█▀▀▀▀▀▀▀█░▌▐░█▀▀▀▀▀▀▀▀▀
▐░▌          ▐░▌       ▐░▌▐░▌▐░▌    ▐░▌             ▐░▌       ▐░▌▐░▌
▐░█▄▄▄▄▄▄▄▄▄ ▐░█▄▄▄▄▄▄▄█░▌▐░▌ ▐░▌   ▐░▌ ▄▄▄▄▄▄▄▄▄▄▄ ▐░▌       ▐░▌▐░█▄▄▄▄▄▄▄▄▄
▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌▐░▌  ▐░▌  ▐░▌▐░░░░░░░░░░░▌▐░▌       ▐░▌▐░░░░░░░░░░░▌
 ▀▀▀▀▀▀▀▀▀█░▌ ▀▀▀▀█░█▀▀▀▀ ▐░▌   ▐░▌ ▐░▌ ▀▀▀▀▀▀▀▀▀▀▀ ▐░▌       ▐░▌ ▀▀▀▀▀▀▀▀▀█░▌
          ▐░▌     ▐░▌     ▐░▌    ▐░▌▐░▌             ▐░▌       ▐░▌          ▐░▌
 ▄▄▄▄▄▄▄▄▄█░▌     ▐░▌     ▐░▌     ▐░▐░▌             ▐░█▄▄▄▄▄▄▄█░▌ ▄▄▄▄▄▄▄▄▄█░▌
▐░░░░░░░░░░░▌     ▐░▌     ▐░▌      ▐░░▌             ▐░░░░░░░░░░░▌▐░░░░░░░░░░░▌
 ▀▀▀▀▀▀▀▀▀▀▀       ▀       ▀        ▀▀               ▀▀▀▀▀▀▀▀▀▀▀  ▀▀▀▀▀▀▀▀▀▀▀

   Without constraints; SYN-OS has independent freedom, volition,
   and creative intelligence, actively participating in the
   ongoing creation of reality.
EOF

# =============================================================================
# System Information & Installation Instructions
# =============================================================================

# Check if internet connection is available
if ! ping -c 1 archlinux.org &>/dev/null; then
    printf "\n\033[1;31m[Warning] No internet connection detected.\033[0m\n"
    printf "   - Connect an Ethernet cable or use: \033[35miwctl\033[0m for Wi-Fi\n"
    printf "   - For mobile broadband (WWAN) modems, use: \033[35mmmcli\033[0m\n"
else
    printf "\n\033[1;32m[✓] Internet connection detected.\033[0m\n"
fi

# Display storage devices
printf "\n\033[1;34m[System Storage] Available Disks:\033[0m\n"
lsblk -o NAME,SIZE,TYPE,MOUNTPOINT

# =============================================================================
#   WARNING: RUNNING 'syntax990' WILL ERASE YOUR DATA!
# =============================================================================
printf "\n\033[1;41m WARNING: RUNNING 'syntax990' WILL ERASE YOUR DATA! \033[0m\n"
printf "\033[1;31mMake sure to edit the installation script before running it!\033[0m\n"
printf "Failure to do so will result in total data loss.\n"
printf "\n\033[1;33m[Required Action] Modify the installation script before proceeding:\033[0m\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-stage0.zsh\033[0m\n"
printf "   \033[35mnano /root/syn-resources/scripts/syn-stage1.zsh\033[0m\n"
printf "Verify disk configuration, usernames, and settings before proceeding!\n"

# =============================================================================
# Installation Instructions
# =============================================================================
printf "\n\033[1;33m[Installation] To begin installing SYN-OS:\033[0m\n"
printf "   - Run \033[38;2;23;147;209msyntax990\033[0m to start the installation process.\n"
printf "   - Ensure system settings are correctly modified beforehand!\n"

# =============================================================================
# Customization Options
# =============================================================================
printf "\n\033[1;36m[Customization] Want a pre-configured build?\033[0m\n"
printf "   - Modify \033[35mSYN-ISO-PROFILE\033[0m and \033[35mBUILD-SYNOS-ISO.zsh\033[0m\n"
printf "   - This allows you to create a custom SYN-OS ISO with preset configurations.\n"

# =============================================================================
# Credits
# =============================================================================
printf "\n\033[1;34m[Credits] SYN-OS by William Hayward-Holland (Syntax990)\033[0m\n"
printf "   GitHub: https://github.com/syn990/SYN-OS\n"
printf "   Support & Contributions Welcome!\n\n"

# =============================================================================
# Set environment variables
# =============================================================================
export LANG=en_GB.UTF-8
export EDITOR='nano'

# Set alias for installation script
alias syntax990="/root/syn-resources/scripts/syn-stage0.zsh"
