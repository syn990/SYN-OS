#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P A C K A G E S
#
#   Package arrays by category (base, networking, shell, desktop, dev
#   tools, fonts, apps). SYNSTALL combines every category for a full
#   install; SYNMINIMAL is the lean test-boot profile below. Categories
#   are an organizational grouping only, not a functional boundary.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PACKAGES (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

baseCore=(
  base                # Minimal package set to define a basic Arch Linux installation
  linux               # The Linux kernel and modules
  linux-firmware      # Firmware files for Linux hardware compatibility
  archlinux-keyring   # Arch Linux PGP keyring for verifying package signatures
  reflector           # Arch Linux mirrorlist generator and ranker (for faster package downloads
  opendoas            # Privilege escalation tool (lightweight sudo alternative)
  qemu-guest-agent    # Host<->guest control channel under QEMU/libvirt (virsh exec/file-transfer,
                      # IP reporting); idles harmlessly with nothing to talk to on real hardware
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
  zram-generator      # systemd generator for zstd-compressed RAM-backed swap (ZramPercent in synos.conf)
)

netAndServices=(
  dhcpcd              # DHCP client daemon for automatic network configuration
  iwd                 # iNet wireless daemon (Wi-Fi management)
  openvpn             # Open source VPN daemon and client
  dnsmasq             # Lightweight DNS and DHCP server (useful for local network services)
  hostapd             # Host Access Point Daemon (turn your machine into a Wi-Fi hotspot)
  openssh             # SSH server/client (sshd, disabled by default; enable with: sudo systemctl enable --now sshd)
  sshfs               # Mount a remote SSH filesystem locally (FUSE-based)
  bluez               # Bluetooth protocol stack (bluetooth.service, disabled by default, same as sshd)
  bluez-utils         # bluetoothctl and friends
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
  glow                      # Markdown renderer for the terminal (Docs menu)
  parted                    # GNU Parted disk partitioning program
)

desktopStack=(
  labwc               # Wayland window-stacking compositor (Openbox alternative)
  wmenu               # Dynamic menu for Wayland (dmenu/tin2 alternative)
  wlr-randr           # Screen management utility for wlroots-based compositors
  grim                # Screenshot utility for Wayland compositors
  slurp               # Selection utility for Wayland compositors (used with grim)
  archlinux-xdg-menu  # Arch Linux menu generator for XDG desktop entries (creates wmenu entries)
  waybar              # Highly customizable Wayland status bar for wlroots-based compositors
  mako                # Lightweight Wayland notification daemon (renders notify-send toasts)
  swaybg              # Background setter for Sway and wlroots-based compositors
  swaylock            # Screen locker for Wayland/wlroots (bound to Super+L in rc.xml, also in the menu)
  fuzzel              # Application launcher for Wayland (bound to Super+A in rc.xml)
  rofi                # Window switcher, application launcher, and dmenu replacement
  feh                 # Lightweight image viewer and background setter
  pavucontrol-qt      # Qt port of the PulseAudio volume controller
  qt5ct               # Qt5 Configuration Utility
  qt6ct               # Qt6 Configuration Utility
  kvantum             # SVG-based theme engine for Qt
  kvantum-qt5         # Qt5 styles for the Kvantum theme engine
  superfile           # Pretty fancy and modern terminal file manager
  qt6-base            # Qt6 core libraries — syn-filemanager (File browser, Super+E) links
                      # against this at runtime; built at ISO-build time, not pacstrap'd as
                      # source, but the shared libraries themselves are still needed here
  lxqt-archiver       # Lightweight archive manager (Qt port of Xarchiver)
  featherpad          # Lightweight text editor for the LXQt desktop environment
)

devToolkit=(
  base-devel          # Full build toolchain (make, patch, pkgconf, gcc, etc.) — makepkg/AUR
                      # building for the user, not needed by the install pipeline itself
                      # anymore (every SYN-SOFTWARE tool ships prebuilt, see syn-pacstrap.zsh)
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

# Lean profile for test-boot installs: base system, networking, shell, and
# the desktop stack (labwc/waybar/etc.) — enough to prove a fresh install
# partitions/formats/mounts/boots and the desktop actually renders, without
# devToolkit or appsMedia's heavier packages (obs-studio, gimp, android-tools,
# archiso...) or noto-fonts-cjk, none of which affect whether that works.
# Same category arrays as SYNSTALL, just fewer of them — add/remove a whole
# category above and both profiles pick it up automatically except where
# excluded here explicitly.
SYNMINIMAL=(
  "${baseCore[@]}"
  "${netAndServices[@]}"
  "${shellAndCLI[@]}"
  "${desktopStack[@]}"
  terminus-font
  ttf-dejavu
  ttf-terminus-nerd
)
