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
# 1) Base Core (kernel, base system, firmware, security-critical essentials)
baseCore=(
  "base"                 # Core Arch Linux base system
  "base-devel"           # Essential development tools (make, gcc, etc.) for base builds
  "linux"                # Linux kernel
  "linux-firmware"       # Common device firmware
  "archlinux-keyring"    # Official Arch Linux package signing keys
  "sudo"                 # Privilege escalation tool
  "sof-firmware"         # Sound Open Firmware
  "sof-tools"            # Sound Open Firmware Tools
  "f2fs-tools"           # F2FS filesystem utilities (mkfs.f2fs, fsck.f2fs)
)

########################################################################################################
# 2) Networking & Services (network stack, VPN, AP, mirrors)
netAndServices=(
  "dhcpcd"     # DHCP client daemon
  "iwd"        # Wi‑Fi management
  "openvpn"    # VPN client
  "dnsmasq"    # Lightweight DNS/DHCP/TFTP
  "hostapd"    # Userspace access point
  "reflector"  # Mirrorlist updater for pacman
)

########################################################################################################
# 3) Shell & CLI (shells, completions, fuzzy finders, core terminal utilities)
shellAndCLI=(
  "zsh"                      # Default user shell
  "zsh-completions"          # Additional completions for Zsh
  "zsh-syntax-highlighting"  # Syntax highlighting for Zsh
  "zsh-autosuggestions"      # Command autosuggestions for Zsh
  "fzf"                      # Command-line fuzzy finder
  "zoxide"                   # Smarter cd alternative
  "ripgrep"                  # Fast text search (grep alternative)
  "fd"                       # Fast file finder (find alternative)
  "bat"                      # cat-like with syntax highlighting
  "inetutils"                # ftp, telnet, hostname, etc.
  "calc"                     # Arbitrary precision console calculator
  "ranger"                   # Terminal file explorer
  "git"                      # Version control
  "btop"                     # Process viewer
  "nano"                     # Console text editor
  "foot"                     # Terminal emulator
  "brightnessctl"            # Brightness Control
  "pamixer"                  # Volume Control
)

########################################################################################################
# 4) Desktop Stack (Wayland session, panels/menus, Qt theming, core GUI tools)
desktopStack=(
  "labwc"                 # Wayland Openbox-like window manager
  "wmenu"                 # Dynamic menu for Wayland
  "archlinux-xdg-menu"    # XDG menu generator (LabWC right-click menu)
  "waybar"                # Panel and taskbar
  "swaybg"                # Wallpaper background for Wayland
  "rofi"                  # Launcher/window switcher (Wayland/X)
  "feh"                   # Image viewer / wallpaper utility
  "pavucontrol-qt"        # Audio mixer (PulseAudio/PipeWire, Qt)
  "qt5ct"                 # Qt5 configuration tool
  "qt6ct"                 # Qt6 configuration tool
  "kvantum"               # Kvantum theme engine for Qt
  "kvantum-qt5"           # Qt5 Kvantum integration
  "pcmanfm-qt"            # File manager (Qt)
  "engrampa"              # Archive manager
  "kwrite"                # Lightweight GUI text editor
  "slurp"                 # Select a region in a Wayland compositor a.k.a Screenshots
)

########################################################################################################
# 5) Developer Toolkit (build tools, device tooling, analysis)
devToolkit=(
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
# 6) Fonts & Internationalisation
fontsI18n=(
  "terminus-font"       # Console font
  "ttf-bitstream-vera"  # TrueType family
  "ttf-dejavu"          # DejaVu family
  "noto-fonts"          # Noto Sans
  "noto-fonts-emoji"    # Emoji
  "noto-fonts-cjk"      # Chinese/Japanese/Korean
  "ttf-liberation"      # Liberation family
  "ttf-terminus-nerd"   # Nerd font variant for UI (e.g., waybar)
  "otf-font-awesome"    # Icon font (UI)
)

########################################################################################################
# 7) Applications (media, creation, browser)
appsMedia=(
  "vlc"         # Media player
  "audacity"    # Audio editor
  "obs-studio"  # Screen recording/streaming
  "chromium"    # Web browser
  "gimp"        # Image editor
  "kdenlive"    # Video editor
)

########################################################################################################
# Combined package list excluding bootloader packages
SYNSTALL=(
  "${baseCore[@]}"
  "${netAndServices[@]}"
  "${shellAndCLI[@]}"
  "${desktopStack[@]}"
  "${devToolkit[@]}"
  "${fontsI18n[@]}"
  "${appsMedia[@]}"
)

# vim: set ft=zsh tw=0 nowrap: