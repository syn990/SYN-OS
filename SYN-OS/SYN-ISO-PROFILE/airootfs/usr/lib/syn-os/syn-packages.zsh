#!/bin/zsh
# Central package arrays. Keep to simple assignments. 
# /usr/lib/syn-os/syn-packages.zsh

baseCore=(
  base
  base-devel
  linux
  linux-firmware
  archlinux-keyring
  opendoas
  sof-firmware
  sof-tools
  # filesystems & block tools
  dosfstools
  e2fsprogs
  f2fs-tools
  btrfs-progs
  xfsprogs
  lvm2
  cryptsetup
  parted
)

netAndServices=(
  dhcpcd
  iwd
  openvpn
  dnsmasq
  hostapd
  reflector
)

shellAndCLI=(
  zsh
  zsh-completions
  zsh-syntax-highlighting
  zsh-autosuggestions
  fzf
  zoxide
  ripgrep
  fd
  bat
  inetutils
  calc
  ranger
  git
  btop
  nano
  foot
  brightnessctl
  pamixer
)

desktopStack=(
  labwc
  wmenu
  archlinux-xdg-menu
  waybar
  swaybg
  rofi
  feh
  pavucontrol-qt
  qt5ct
  qt6ct
  kvantum
  kvantum-qt5
  pcmanfm-qt
  engrampa
  kwrite
  slurp
)

devToolkit=(
  gcc
  fakeroot
  android-tools
  archiso
  binwalk
  hexedit
  lshw
  yt-dlp
)

fontsI18n=(
  terminus-font
  ttf-bitstream-vera
  ttf-dejavu
  noto-fonts
  noto-fonts-emoji
  noto-fonts-cjk
  ttf-liberation
  ttf-terminus-nerd
  otf-font-awesome
)

appsMedia=(
  vlc
  audacity
  obs-studio
  chromium
  gimp
  kdenlive
)

SYNSTALL=(
  "${baseCore[@]}"
  "${netAndServices[@]}"
  "${shellAndCLI[@]}"
  "${desktopStack[@]}"
  "${devToolkit[@]}"
  "${fontsI18n[@]}"
  "${appsMedia[@]}"
)