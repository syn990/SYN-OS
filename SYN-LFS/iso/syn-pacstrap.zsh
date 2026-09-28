#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#   syn-pacstrap.zsh, SYN-OS on runit
#
#   What pacstrapMain does on this edition. The Arch installer installs its
#   packages with pacstrap; here every package is in the ISO's root image
#   already, so the image is copied onto the new root. Then the same things
#   the Arch file does after pacstrap: fstab, the answers file, and the
#   install state Stage 1 reads. syn-stage0.zsh sources this in place of
#   the Arch file; the disk work before it (syn-disk.zsh) is shared as is.
# ------------------------------------------------------------------------------
set -euo pipefail

Source=/run/syn/rootfs      # the ISO's root image, mounted by the live init

# fstab line for a filesystem, by UUID; ext4 gets fsck'd at boot, the rest
# check themselves
fstabLine() {
  local dev=$1 mnt=$2 fs=$3 opts=$4 pass=0
  [ "$fs" = ext4 ] && pass=1
  printf 'UUID=%s  %s  %s  %s  0 %s\n' "$(blkid -s UUID -o value "$dev")" "$mnt" "$fs" "$opts" "$pass"
}

pacstrapMain() {
  local target=$RootMountLocation kernel bootFs
  [ -d "$Source/usr" ] || { syn_ui::error "No root image at $Source: synos-install runs from the SYN-OS ISO"; exit 1; }

  syn_ui::step "Copying SYN-OS onto ${RootFsDev} (several GB, a few minutes)"
  # -a keeps ownership and modes. No ACLs or xattrs: the image has none,
  # and the ESP (FAT) under /boot could not hold them
  rsync -a --info=progress2 --no-inc-recursive "$Source/" "$target/"
  syn_ui::step_done "System copied"

  # The kernel under the name the boot entries use (the Arch package's), a
  # copy rather than a link because /boot may be the FAT ESP. mkinitcpio's
  # preset builds /boot/initramfs-linux.img from it in Stage 1
  kernel=("$target"/boot/vmlinuz-*-syn(N))
  (( $#kernel )) || { syn_ui::error "No kernel in $target/boot"; exit 1; }
  cp -f "$kernel[1]" "$target/boot/vmlinuz-linux"

  # Stage 1 creates the user from synos.conf; an account the root image
  # carried (build.zsh user) would make it skip that, and with no home
  local name
  for name in $(awk -F: '$3 >= 1000 && $3 < 65534 { print $1 }' "$target/etc/passwd"); do
    userdel -R "$target" "$name"
  done

  syn_ui::step "Writing fstab"
  case "${PartitionStrat}" in
    uefi-*)         bootFs="vfat  defaults,umask=0077" ;;
    mbr-grub)       bootFs="ext4  defaults" ;;
    mbr-grub-btrfs) bootFs="btrfs defaults" ;;
    mbr-grub-xfs)   bootFs="xfs   defaults" ;;
    *)              bootFs="" ;;
  esac
  {
    echo "# SYN-OS: written by synos-install, by UUID"
    fstabLine "$RootFsDev" / "$FilesystemStrat" defaults
    if [ -n "${BootPart:-}" ] && [ "$BootPart" != "$RootPart" ] && [ -n "$bootFs" ]; then
      fstabLine "$BootPart" /boot ${=bootFs}
    fi
    [ -n "${SwapDev:-}" ] && printf 'UUID=%s  none  swap  defaults  0 0\n' "$(blkid -s UUID -o value "$SwapDev")"
    # The kernel filesystems, as the image's own fstab lists them
    grep -E '^(proc|sysfs|devpts|tmpfs|devtmpfs|cgroup2)[[:space:]]' "$Source/etc/fstab"
  } > "$target/etc/fstab"
  syn_ui::step_done "fstab written"

  # The answers go with the install, minus the LUKS passphrase; Stage 1
  # removes the account password once it has used it
  mkdir -p "$target/etc/syn-os"
  sed -e '/^LuksPassphrase=/d' "$SYNOS_CONF" > "$target/etc/syn-os/synos.conf"

  # Every install gets its own machine id, not the image's
  rm -f "$target/var/lib/dbus/machine-id"
  dbus-uuidgen --ensure="$target/var/lib/dbus/machine-id"

  local State="$target/etc/syn-os/install.state"
  cat > "$State" <<EOF
BootPart="${BootPart:-}"
RootPart="${RootPart:-}"
RootMapper="${RootMapper:-}"
RootFsDev="${RootFsDev}"
SwapDev="${SwapDev:-}"
LuksUuid="${LuksUuid:-}"
EOF
  chmod 600 "$State"
  syn_ui::step_done "System in place, state saved for Stage 1"
}
