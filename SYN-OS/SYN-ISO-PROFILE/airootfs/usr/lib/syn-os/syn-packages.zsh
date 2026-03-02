#!/bin/zsh
# Central package arrays. Keep to simple assignments. 
# /usr/lib/syn-os/syn-packages.zsh

# This file defines arrays of packages for different categories (base system, networking, shell tools, desktop environment, development tools, fonts, and applications).
# Treat this as a menu of packages to choose from when building the Arch Linux installation. The final SYNSTALL array combines all these categories for installation.
# Add and remove packages from the individual category arrays as needed to customize the installation. The SYNSTALL array will automatically include all packages from the categories.

# There is no special reason for the categorization of packages into these arrays. They are simply grouped for organizational purposes.

baseCore=(
  base                # Minimal package set to define a basic Arch Linux installation
  base-devel          # Essential tools for building packages (makepkg, gcc, make, etc.)
  linux               # The Linux kernel and modules
  linux-firmware      # Firmware files for Linux hardware compatibility
  archlinux-keyring   # Arch Linux PGP keyring for verifying package signatures
  reflector           # Arch Linux mirrorlist generator and ranker (for faster package downloads
  opendoas            # Privilege escalation tool (lightweight sudo alternative)
  sof-firmware        # Sound Open Firmware (modern audio drivers)
  sof-tools           # Tools and utilities for Sound Open Firmware
  # filesystems & block tools
  fuse                # Filesystem in Userspace (FUSE) library and utilities
  dosfstools          # Utilities for creating and checking DOS FAT filesystems
  e2fsprogs           # Ext2/3/4 filesystem utilities
  f2fs-tools          # Tools for Flash-Friendly File System (F2FS)
  btrfs-progs         # Btrfs filesystem utilities
  xfsprogs            # XFS filesystem utilities
  lvm2                # Logical Volume Manager 2 utilities
  cryptsetup          # Disk encryption tool (LUKS)
  parted              # GNU Parted disk partitioning program
)

netAndServices=(
  dhcpcd              # DHCP client daemon for automatic network configuration
  iwd                 # iNet wireless daemon (Wi-Fi management)
  openvpn             # Open source VPN daemon and client
  dnsmasq             # Lightweight DNS and DHCP server (useful for local network services)
  hostapd             # Host Access Point Daemon (turn your machine into a Wi-Fi hotspot)
)

shellAndCLI=(
  zsh                       # Advanced command interpreter (shell)
  zsh-completions           # Additional completion definitions for Zsh
  zsh-syntax-highlighting   # Fish-shell like syntax highlighting for Zsh
  zsh-autosuggestions       # Fish-like fast/unobtrusive autosuggestions for Zsh
  fzf                       # Command-line fuzzy finder
  zoxide                    # Smarter cd command (directory jumper)
  ripgrep                   # Extremely fast grep alternative
  fd                        # Simple, fast and user-friendly alternative to 'find'
  bat                       # Cat clone with syntax highlighting and Git integration
  inetutils                 # Collection of common network programs (ping, ftp, telnet)
  calc                      # Arbitrary precision console calculator
  git                       # Distributed version control system
  btop                      # Resource monitor (CPU, memory, disks, network, processes)
  nano                      # Easy-to-use command line text editor
  foot                      # Lightweight Wayland terminal emulator
  brightnessctl             # Tool to read and control screen brightness
  pamixer                   # PulseAudio command-line mixer
)

desktopStack=(
  labwc               # Wayland window-stacking compositor (Openbox alternative)
  wmenu               # Dynamic menu for Wayland (dmenu/tin2 alternative)
  wlr-randr           # Screen management utility for wlroots-based compositors
  grim                # Screenshot utility for Wayland compositors
  slurp               # Selection utility for Wayland compositors (used with grim)
  archlinux-xdg-menu  # Arch Linux menu generator for XDG desktop entries (creates wmenu entries)
  waybar              # Highly customizable Wayland status bar for wlroots-based compositors
  swaybg              # Background setter for Sway and wlroots-based compositors
  rofi                # Window switcher, application launcher, and dmenu replacement
  feh                 # Lightweight image viewer and background setter
  pavucontrol-qt      # Qt port of the PulseAudio volume controller
  qt5ct               # Qt5 Configuration Utility
  qt6ct               # Qt6 Configuration Utility
  kvantum             # SVG-based theme engine for Qt
  kvantum-qt5         # Qt5 styles for the Kvantum theme engine
  superfile           # Pretty fancy and modern terminal file manager
  lxqt-archiver       # Lightweight archive manager (Qt port of Xarchiver)
  featherpad          # Lightweight text editor for the LXQt desktop environment
)

devToolkit=(
  gcc                 # The GNU Compiler Collection (C, C++, etc.)
  fakeroot            # Tool for simulating superuser privileges (needed for makepkg)
  android-tools       # Android platform tools (adb, fastboot)
  archiso             # Tools for creating Arch Linux live and install ISO images
  binwalk             # Tool for searching a given binary image for embedded files
  hexedit             # View and edit files in hexadecimal or ASCII
  lshw                # Utility to extract detailed hardware configuration
  yt-dlp              # Command-line audio/video downloader (youtube-dl fork)
)

fontsI18n=(
  terminus-font       # The best font for terminal use (monospaced, clean, and highly readable)
  ttf-bitstream-vera  # Bitstream Vera fonts
  ttf-dejavu          # Font family based on Bitstream Vera (high unicode coverage)
  noto-fonts          # Google Noto TTF fonts (Latin, Greek, Cyrillic)
  noto-fonts-emoji    # Google Noto emoji fonts
  noto-fonts-cjk      # Google Noto CJK fonts (Chinese, Japanese, Korean)
  ttf-liberation      # Liberation fonts (metric-compatible with Arial, Times New Roman, Courier New)
  ttf-terminus-nerd   # Terminus font patched with Nerd Font glyphs (icons)
  otf-font-awesome    # Iconic font and CSS toolkit
)

appsMedia=(
  vlc                 # Multi-platform media player
  openra              # Open source reimplementation of classic real-time strategy games (Red Alert, Tiberian Dawn, Dune 2000)
  audacity            # Digital audio editor and recording application
  obs-studio          # Free and open source software for video recording and live streaming
  falkon              # Lightweight web browser based on QtWebEngine
  gimp                # GNU Image Manipulation Program (raster graphics editor)
)

# Combined array holding all packages for installation
SYNSTALL=(
  "${baseCore[@]}"
  "${netAndServices[@]}"
  "${shellAndCLI[@]}"
  "${desktopStack[@]}"
  "${devToolkit[@]}"
  "${fontsI18n[@]}"
  "${appsMedia[@]}"
)
