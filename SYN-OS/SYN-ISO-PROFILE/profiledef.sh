# =============================================================================
#                              SYN-OS profiledef.sh
#         Build Configuration File for SYN-OS ISO Build with Custom Title
# -----------------------------------------------------------------------------
#   This configuration file defines the settings used during the SYN-OS ISO
#   build process. It includes custom titles and configurations.
#   Author: Syntax990
# =============================================================================

iso_name="SYN-OS-M-141"
iso_label="SYNOS_$(date +%Y%m)"
iso_publisher="Syntax990"
iso_application="SYN-OS - Installation Media"
iso_version="$(date +%Y.%m.%d)"
install_dir="arch"
buildmodes=('iso')
bootmodes=(
  'bios.syslinux.mbr'
  'bios.syslinux.eltorito'
  'uefi-ia32.grub.esp'
  'uefi-x64.grub.esp'
  'uefi-ia32.grub.eltorito'
  'uefi-x64.grub.eltorito'
)
arch="x86_64"
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'xz' '-b' '256K' '-Xbcj' 'x86')

# File Permissions
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/root/syn-resources/scripts/syn-stage0.zsh"]="0:0:755"
  ["/root/syn-resources/scripts/syn-lanbridge.sh"]="0:0:755"
  ["/root/syn-resources/scripts/syn-stage1.zsh"]="0:0:755"
)
