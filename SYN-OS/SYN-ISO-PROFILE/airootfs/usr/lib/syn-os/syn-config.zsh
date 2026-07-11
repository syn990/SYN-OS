#!/bin/zsh
# /usr/lib/syn-os/syn-config.zsh
# Canonical config loader for SYN‑OS (CamelCase only)
set -euo pipefail

: "${SYNOS_CONF:=/etc/syn-os/synos.conf}"

if [ ! -r "$SYNOS_CONF" ]; then
  echo "ERROR: Missing SYN‑OS config at $SYNOS_CONF" >&2
  exit 1
fi

# Load user config (CamelCase variables only)
# shellcheck disable=SC1090
source "$SYNOS_CONF"

# --- helpers ---------------------------------------------------------------
toYesNo() {
  # normalise common boolean spellings to yes|no
  # usage: toYesNo "$var"
  local v="${1:-}"
  case "${v:l}" in
    y|yes|true|1)  printf 'yes';;
    n|no|false|0|'') printf 'no';;
    *) printf '%s' "$v";;
  esac
}

lower() { print -r -- "${1:-}" | tr '[:upper:]' '[:lower:]'; }

# --- normalise/derive ------------------------------------------------------
# Booleans -> yes|no
RequireWipeConfirm="$(toYesNo "${RequireWipeConfirm:-yes}")"

# Default shell if omitted
: "${UserShell:=/bin/zsh}"

# Firmware mode: detect real UEFI vs BIOS unless BootMode overrides it.
if [ "$(lower "${BootMode:-auto}")" = "auto" ]; then
  if [ -d /sys/firmware/efi/efivars ]; then
    SynosEnv="UEFI"
  else
    SynosEnv="MBR"
  fi
else
  case "$(lower "${BootMode}")" in
    uefi) SynosEnv="UEFI" ;;
    bios|mbr|legacy) SynosEnv="MBR" ;;
    *) echo "ERROR: BootMode must be one of: auto|UEFI|MBR" >&2; exit 1 ;;
  esac
fi

# Encryption/UseLvm: dead-simple yes|no flags, normalised early since
# PartitionStrat=auto (below) needs Encryption to pick between mbr-syslinux
# and mbr-grub on BIOS.
Encryption="$(toYesNo "${Encryption:-no}")"
UseLvm="$(toYesNo "${UseLvm:-no}")"
EnableSsh="$(toYesNo "${EnableSsh:-no}")"

# --- sanity checks (fast, strict) -----------------------------------------
# Required identity + input
: "${Hostname:?Hostname not set}"
: "${UserAccountName:?UserAccountName not set}"
: "${Locale:?Locale not set}"
: "${LocaleGen:?LocaleGen not set}"
: "${KeyMap:?KeyMap not set}"
: "${TimeZone:?TimeZone not set}"
: "${VconsoleFont:?VconsoleFont not set}"

# Disk and filesystems
[ -b "${Disk:-}" ] || { echo "ERROR: Disk '${Disk:-}' is not a block device" >&2; exit 1; }

case "${FilesystemStrat:-}" in
  ext4|f2fs|btrfs|xfs) : ;;
  *) echo "ERROR: Unsupported FilesystemStrat '${FilesystemStrat:-}'" >&2; exit 1 ;;
esac

case "$(lower "${PackageProfile:-full}")" in
  full)    PackageProfile="full" ;;
  minimal) PackageProfile="minimal" ;;
  *) echo "ERROR: Unknown PackageProfile '${PackageProfile:-}' (must be full|minimal)" >&2; exit 1 ;;
esac

# PartitionStrat=auto resolves against the firmware SynosEnv actually
# detected above, instead of shipping a hardcoded guess in synos.conf that
# can silently mismatch real hardware (e.g. a default of uefi-bootctl
# surviving onto legacy BIOS: parted doesn't care and will happily write a
# GPT+ESP layout, and bootctl's --graceful-in-chroot behavior means the
# doomed install may not even error — just produce an unbootable disk).
if [ "$(lower "${PartitionStrat:-auto}")" = "auto" ]; then
  if [ "$SynosEnv" = "UEFI" ]; then
    PartitionStrat="uefi-bootctl"
  elif [ "$Encryption" = "yes" ]; then
    PartitionStrat="mbr-grub"
  else
    PartitionStrat="mbr-syslinux"
  fi
fi

# Strategy validation
case "${PartitionStrat:-}" in
  uefi-bootctl|mbr-syslinux|mbr-grub) : ;;
  *) echo "ERROR: Unknown PartitionStrat '${PartitionStrat:-}'" >&2; exit 1 ;;
esac

# Catch an explicit PartitionStrat that doesn't match detected firmware —
# this is the exact mismatch that silently produced unbootable installs
# before PartitionStrat=auto existed, and it's still possible to hit if
# someone hardcodes a value that doesn't suit the machine they're installing
# on (e.g. copying a synos.conf from a UEFI machine onto BIOS hardware).
if [ "$PartitionStrat" = "uefi-bootctl" ] && [ "$SynosEnv" != "UEFI" ]; then
  echo "ERROR: PartitionStrat=uefi-bootctl but this machine booted in BIOS/legacy mode (no /sys/firmware/efi/efivars)." >&2
  echo "Use PartitionStrat=mbr-syslinux or mbr-grub, or set PartitionStrat=auto to detect automatically." >&2
  exit 1
fi
if { [ "$PartitionStrat" = "mbr-syslinux" ] || [ "$PartitionStrat" = "mbr-grub" ]; } && [ "$SynosEnv" = "UEFI" ]; then
  echo "ERROR: PartitionStrat=${PartitionStrat} but this machine booted in UEFI mode." >&2
  echo "Use PartitionStrat=uefi-bootctl, or set PartitionStrat=auto to detect automatically." >&2
  exit 1
fi

if [ "$Encryption" = "yes" ] && [ "$UseLvm" = "yes" ]; then
  VolumeStrat="luks-lvm"
elif [ "$Encryption" = "yes" ]; then
  VolumeStrat="luks-only"
elif [ "$UseLvm" = "yes" ]; then
  VolumeStrat="lvm-only"
else
  VolumeStrat="plain"
fi

# syslinux has no LUKS support at all — it cannot read from an encrypted
# partition under any circumstances, and mbr-syslinux has no separate boot
# partition to fall back on. Encrypted BIOS/MBR installs must use mbr-grub
# instead (its unencrypted /boot partition means GRUB never has to touch
# encryption directly — the initramfs unlocks root at boot, same as it does
# for uefi-bootctl), or use uefi-bootctl itself.
if [ "$PartitionStrat" = "mbr-syslinux" ] && [ "$Encryption" = "yes" ]; then
  echo "ERROR: PartitionStrat=mbr-syslinux cannot use Encryption=yes — syslinux has no LUKS support." >&2
  echo "Use PartitionStrat=mbr-grub for encrypted BIOS/MBR installs, or PartitionStrat=uefi-bootctl." >&2
  exit 1
fi

# Mount layout
: "${RootMountLocation:?RootMountLocation not set}"
: "${BootMountLocation:?BootMountLocation not set}"
case "${BootMountLocation}" in
  "${RootMountLocation}"/*) : ;;
  *) echo "ERROR: BootMountLocation must be under RootMountLocation" >&2; exit 1 ;;
esac

# LVM constraints
case "${VolumeStrat}" in
  luks-lvm|lvm-only)
    : "${VgName:?VgName not set for LVM strategy}"
    : "${LvRootName:?LvRootName not set for LVM strategy}"
    ;;
esac

# LUKS constraints
case "${VolumeStrat}" in
  luks-lvm|luks-only)
    : "${LuksLabel:?LuksLabel not set for LUKS strategy}"
    : "${LuksCipher:?LuksCipher not set for LUKS strategy}"
    : "${LuksKeySize:?LuksKeySize not set for LUKS strategy}"
    : "${LuksPbkdf:?LuksPbkdf not set for LUKS strategy}"
    : "${LuksPassphrase:?LuksPassphrase not set for LUKS strategy}"
    if [ "$LuksPassphrase" = "CHANGE_ME" ]; then
      echo "ERROR: LuksPassphrase is still 'CHANGE_ME' in synos.conf — set a real passphrase before installing." >&2
      exit 1
    fi
    ;;
esac

# --- export for Stage 0/1 --------------------------------
export \
  SynosEnv \
  Hostname UserAccountName UserShell \
  Locale LocaleGen KeyMap TimeZone VconsoleFont \
  Disk BootMode BootSize \
  PartitionStrat VolumeStrat FilesystemStrat BootloaderStrat PackageProfile \
  Encryption UseLvm EnableSsh \
  VgName LvRootName LvSwapName SwapSize \
  BootFs RootFs \
  RootMountLocation BootMountLocation \
  LuksCipher LuksKeySize LuksPbkdf LuksLabel LuksPassphrase \
  KernelOpts \
  RequireWipeConfirm