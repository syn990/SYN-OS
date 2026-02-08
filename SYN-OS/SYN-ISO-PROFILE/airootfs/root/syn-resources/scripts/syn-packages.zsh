#!/bin/zsh
# =============================================================================
#                           SYN-OS Package Config
#
# Purpose:
#   Central place for package arrays used by the installer and ISO build.
#   Both stage0 and stage1 source this file to keep definitions consistent.
#
# Guidance:
#   Keep this file to simple array assignments. Avoid commands or conditionals.
#   Putting logic here can cause side effects when sourced and may break things.
#   Put core logic in the staging scripts:
#     - stage0  (pre-chroot)
#     - stage1  (post-chroot)
#
# Mirrors and availability:
#   All packages are vanilla Arch. Availability depends on the pacman
#   mirrorlist at build or install time. Using reflector only refreshes
#   mirrors and does not change these definitions.
#
# Bootloader note:
#   Do not list bootloader packages here. They are appended conditionally
#   in syn-stage0.zsh based on detected firmware (UEFI or MBR).
#
# Meta:
#   SYN-OS      : The Syntax Operating System
#   Author      : William Hayward-Holland (Syntax990)
#   License     : MIT License
# =============================================================================

########################################################################################################
# Core System
coreSystem=(
    "base"                  # Core Arch Linux base system
    "base-devel"            # Essential development tools (make, gcc, etc.)
    "bat"                   # cat-like application
    "linux"                 # Linux kernel
    "linux-firmware"        # Common device firmware
    "archlinux-keyring"     # Official Arch Linux package signing keys
    "zsh"                   # Default user shell
    "zsh-completions"       # Additional completions for Zsh
    "zsh-syntax-highlighting" # Syntax highlighting for Zsh
    "zsh-autosuggestions"   # Command autosuggestions for Zsh
    "fzf"                   # Command-line fuzzy finder
    "zoxide"                # Smarter cd command replacement
    "ripgrep"               # Fast text search tool (like grep)
    "fd"                    # Simple, fast alternative to find
    "sudo"                  # Privilege escalation tool
)

########################################################################################################
# Services
services=(
    "dhcpcd"     # DHCP client daemon
    "dnsmasq"    # Lightweight DNS DHCP TFTP
    "hostapd"    # Userspace access point
    "iwd"        # Wi-Fi management
    "reflector"  # Mirrorlist updater for pacman
    "openvpn"    # VPN client
)

########################################################################################################
# Environment & Shell
environmentShell=(
    "labwc"             # Wayland Openbox-like window manager
    "wmenu"             # Dynamic menu for Wayland
    "waybar"            # Panel and taskbar
    "pavucontrol-qt"    # PulseAudio configuration tool
    "qt5ct"             # Qt5 configuration tool
    "qt6ct"             # Qt6 configuration tool
    "kvantum"           # Kvantum theme engine for Qt
    "kvantum-qt5"       # Qt5 Kvantum integration
    "feh"               # Image viewer and wallpaper
    "kitty"             # Terminal emulator
    "inetutils"         # ftp, telnet, hostname, etc.
    "rofi"              # Window switcher, run dialog, ssh-launcher and dmenu replacement
    "calc"              # Arbitrary precision console calculator
)

########################################################################################################
# User Applications
userApplications=(
    "nano"        # Text editor
    "git"         # Version control
    "htop"        # Process viewer
    "pcmanfm-qt"  # File manager
    "engrampa"    # Archive manager
    "kwrite"      # Text editor
    "ranger"      # Terminal File Explorer
)

########################################################################################################
# Developer Tools
developerTools=(
    "gcc"            # Compiler collection
    "fakeroot"       # Fake root for builds
    "android-tools"  # ADB and fastboot
    "archiso"        # Arch ISO tooling
    "binwalk"        # Firmware analysis
    "hexedit"        # Hex editor
    "lshw"           # Hardware lister
    "yt-dlp"         # Media downloader
)

########################################################################################################
# Fonts & Localisation
fontsLocalization=(
    "terminus-font"      # Console font
    "ttf-bitstream-vera" # TrueType family
    "ttf-dejavu"         # DejaVu family
    "noto-fonts"         # Noto Sans
    "noto-fonts-emoji"   # Emoji
    "noto-fonts-cjk"     # Chinese Japanese Korean
    "ttf-liberation"     # Liberation family
    "ttf-terminus-nerd"  # Awesome family of fonts for waybar (UI)
    "otf-font-awesome"   # Additional Nerd Fonts (UI)
)

########################################################################################################
# Optional Features
optionalFeatures=(
    "vlc"        # Media player
    "audacity"   # Audio editor
    "obs-studio" # Screen recorder streaming
    "chromium"   # Web browser
    "gimp"       # Image editor
    "kdenlive"   # Video editor
)

########################################################################################################
# Combined package list excluding bootloader packages
SYNSTALL=(
    "${coreSystem[@]}"
    "${services[@]}"
    "${environmentShell[@]}"
    "${userApplications[@]}"
    "${developerTools[@]}"
    "${fontsLocalization[@]}"
    "${optionalFeatures[@]}"
)

# vim: set ft=zsh tw=0 nowrap: