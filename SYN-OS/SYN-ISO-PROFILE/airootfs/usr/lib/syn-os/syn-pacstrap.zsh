#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P A C S T R A P
#
#   Runs pacstrap onto the target root, generates fstab, deploys the
#   dotfile overlay and docs, and writes install.state for Stage 1 to
#   pick up after chrooting.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PACSTRAP (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

# =========================================================
# Base install + state persistence
# =========================================================
pacstrapMain() {
  source /usr/lib/syn-os/syn-packages.zsh

  syn_ui::pacman_snack

  # Mirror + keyring
  syn_ui::step "Refreshing mirrors and pacman keyring"
  reflector -c GB -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist || true
  pacman-key --init
  pacman-key --populate archlinux
  pacman -Sy
  syn_ui::step_done "Mirrors and keyring ready"

  # Select bootloader packages
  local -a bootPkgs
  case "${BootloaderStrat}" in
    auto)
      case "${PartitionStrat}" in
        uefi-bootctl) bootPkgs=(efibootmgr systemd) ;;
        mbr-grub)     bootPkgs=(grub) ;;
        *)            bootPkgs=(syslinux) ;;
      esac
      ;;
    systemd-boot) bootPkgs=(efibootmgr systemd) ;;
    syslinux)     bootPkgs=(syslinux) ;;
    grub)         bootPkgs=(grub) ;;
    *)            bootPkgs=() ;;
  esac

  # Only place full (SYNSTALL) vs minimal (SYNMINIMAL) profiles diverge —
  # same install pipeline either way, just a different package array.
  local -a packageList
  case "${PackageProfile}" in
    minimal) packageList=("${SYNMINIMAL[@]}") ;;
    *)       packageList=("${SYNSTALL[@]}") ;;
  esac
  packageList+=("${bootPkgs[@]}")

  syn_ui::step "Installing packages to ${RootMountLocation} (profile: ${PackageProfile})"
  pacstrap -K "${RootMountLocation}" "${packageList[@]}"
  genfstab -U "${RootMountLocation}" >> "${RootMountLocation}/etc/fstab"
  syn_ui::step_done "Base packages installed"

  # UserAccountPassword travels in this copy so Stage 1 can chpasswd with
  # it via syn-config.zsh — Stage 1 strips it from disk once used.
  # LuksPassphrase, unlike the account password, is already fully consumed
  # by this point (cryptsetup luksFormat/open ran earlier in this same
  # Stage 0 pipeline) — it never needs to reach the target disk at all, so
  # strip it from the copy source before install rather than have Stage 1
  # clean up a copy that was never necessary.
  sed -i '/^LuksPassphrase=/d' /etc/syn-os/synos.conf
  install -Dm644 /etc/syn-os/synos.conf "${RootMountLocation}/etc/syn-os/synos.conf"
  for script in /usr/lib/syn-os/*.zsh; do
    install -Dm755 "$script" "${RootMountLocation}/usr/lib/syn-os/$(basename "$script")"
  done

  if [ -d /usr/lib/syn-os/DotfileOverlay ]; then
    syn_ui::info "Deploying dotfile overlay to ${RootMountLocation}…"
    cp -r /usr/lib/syn-os/DotfileOverlay/* "${RootMountLocation}/"
    chmod -R +x "${RootMountLocation}/usr/lib/syn-os"
    chmod -R +x "${RootMountLocation}/usr/local/bin"
    chmod -R +x "${RootMountLocation}/etc/skel/.config/labwc"
    chmod -R +x "${RootMountLocation}/etc/skel/.config/waybar"
    chmod -R +x "${RootMountLocation}/etc/skel/.config/superfile"
  fi

  # Docs are static system data, not a per-user dotfile, so they get their
  # own copy to /usr/share rather than living inside DotfileOverlay above.
  if [ -d /usr/share/syn-os/docs ]; then
    syn_ui::info "Deploying docs to ${RootMountLocation}/usr/share/syn-os/docs…"
    mkdir -p "${RootMountLocation}/usr/share/syn-os"
    cp -r /usr/share/syn-os/docs "${RootMountLocation}/usr/share/syn-os/docs"
  fi

  # Persist state for Stage 1 — only facts stage0 computed at runtime
  # (actual partition devices, the LUKS UUID cryptsetup just generated).
  # Everything else Stage 1 needs (Hostname, KeyMap, UserAccountPassword...)
  # is already in the synos.conf copy above, which Stage 1 re-sources via
  # syn-config.zsh the same way Stage 0 did.
  local State="${RootMountLocation}/etc/syn-os/install.state"
  mkdir -p "$(dirname "$State")"
  cat > "$State" <<EOF
BootPart="${BootPart:-}"
RootPart="${RootPart:-}"
RootMapper="${RootMapper:-}"
RootFsDev="${RootFsDev}"
SwapDev="${SwapDev:-}"
LuksUuid="${LuksUuid:-}"
EOF
  chmod 600 "$State"

  syn_ui::step_done "Base install complete, state saved for Stage 1"
}
