# =============================================================================
#                              SYN-OS profiledef.sh
#         Build Configuration File for SYN-OS ISO Build with Custom Title
# -----------------------------------------------------------------------------
#   This configuration file defines the settings used during the SYN-OS ISO
#   build process. It includes custom titles and configurations.
#   Author: Syntax990
# =============================================================================

iso_name="SYN-OS-SYNAPTIC-EDITION"
iso_label="SYNOS_$(date +%Y%m)"
iso_publisher="Syntax990"
iso_application="SYN-OS - Installation Media"
iso_version="$(date +%Y.%m.%d)"
install_dir="arch"
buildmodes=('iso')
bootmodes=(
  'bios.syslinux'
  'uefi.systemd-boot'
)
arch="x86_64"
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'xz' '-b' '256K' '-Xbcj' 'x86')

# File Permissions
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/usr/lib/syn-os/syn-stage0.zsh"]="0:0:755"
  ["/usr/lib/syn-os/syn-stage1.zsh"]="0:0:755"
  ["/usr/lib/syn-os/syn-config.zsh"]="0:0:755"
  ["/usr/lib/syn-os/syn-packages.zsh"]="0:0:755"
  ["/usr/lib/syn-os/ui.zsh"]="0:0:755"
)
