#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="SYN-OS"
iso_label="SYN-OS"
iso_publisher="Syntax990"
desktop="Install Image"
iso_application="SYN-RTOS-2035 Installation Enviroment"
iso_version="2035"
install_dir="arch"
buildmodes=('iso')
bootmodes=('bios.syslinux.mbr' 'bios.syslinux.eltorito'
           'uefi-ia32.grub.esp' 'uefi-x64.systemd-boot.esp'
           'uefi-ia32.grub.eltorito' 'uefi-x64.systemd-boot.eltorito')
arch="x86_64"
pacman_conf="pacman.conf"
pacman_testing_conf="pacman-testing.conf"

# The type of filesystem image to be created
# Only squashfs is generally used for this purpose
airootfs_image_type="squashfs"
#airootfs_image_type="alternative_filesystem"

# Options to be passed to the image creation tool
airootfs_image_tool_options=('-comp' 'lz4')

# Different compression algorithms, commented out
#airootfs_image_tool_options=('-comp' 'gzip')
#airootfs_image_tool_options=('-comp' 'lzo')
#airootfs_image_tool_options=('-comp' 'xz')
#airootfs_image_tool_options=('-comp' 'lzma')
#airootfs_image_tool_options=('-comp' 'zstd', '-Xcompression-level', '22')

file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/root"]="0:0:750"
  ["/root/SYN-INSTALLER-SCRIPTS/motd-primer.sh"]="0:0:750"
  ["/root/SYN-INSTALLER-SCRIPTS/motd.sh"]="0:0:750"
  ["/root/SYN-INSTALLER-SCRIPTS/syn-1_chroot.sh"]="0:0:750"
  ["/root/SYN-INSTALLER-SCRIPTS/syn-ascii-art.sh"]="0:0:750"
  ["/root/SYN-INSTALLER-SCRIPTS/syn-disk-variables.sh"]="0:0:750"
  ["/root/SYN-INSTALLER-SCRIPTS/syn-installer-functions.sh"]="0:0:750"
  ["/root/SYN-INSTALLER-SCRIPTS/SYN-INSTALLER-MAIN.sh"]="0:0:750"
  ["/root/SYN-INSTALLER-SCRIPTS/syn-pacstrap-variables.sh"]="0:0:750"
)

