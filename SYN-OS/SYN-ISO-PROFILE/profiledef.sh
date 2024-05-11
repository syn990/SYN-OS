#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="SYN-OS"
iso_label=""
iso_publisher="Syntax990"
iso_application="SYN-OS"
iso_version="VOLITION"
install_dir="arch"
buildmodes=('iso')
bootmodes=('bios.syslinux.mbr' 'bios.syslinux.eltorito'
           'uefi-ia32.grub.esp' 'uefi-x64.grub.esp'
           'uefi-ia32.grub.eltorito' 'uefi-x64.grub.eltorito')
arch="x86_64"
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'lz4')
#airootfs_image_type="erofs"
#airootfs_image_tool_options=('-zlzma,109' -E 'ztailpacking,fragments,dedupe')
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/root/syn-resources/scripts/syn-stage0.zsh"]="0:0:755"
  ["/root/syn-resources/scripts/syn-mbr.zsh"]="0:0:755"
  ["/root/syn-resources/scripts/syn-uefi.zsh"]="0:0:755"
)
