#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                           S Y N - C O N F I G
#
#   Canonical config loader for SYN-OS: reads synos.conf, normalizes and
#   validates every setting (CamelCase variables only), and exports the
#   result for syn-stage0.zsh / syn-stage1.zsh to consume.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-CONFIG (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
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

# UserAccountPassword is only actually consumed by Stage 1, inside the
# chroot — but checked here too, before Stage 0 touches the disk at all.
# Catching this after partitioning/pacstrap (Stage 1's own check, kept as
# defense-in-depth) means a forgotten password wipes the disk for nothing.
: "${UserAccountPassword:?UserAccountPassword not set}"
if [ "$UserAccountPassword" = "CHANGE_ME" ]; then
  echo "ERROR: UserAccountPassword is still 'CHANGE_ME' in synos.conf — set a real password before installing." >&2
  exit 1
fi

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

# Resolves against the detected SynosEnv rather than trusting a hardcoded
# guess in synos.conf — a stale uefi-bootctl value on real BIOS hardware
# can produce an unbootable disk without erroring.
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

# Catches an explicit PartitionStrat that doesn't match detected firmware
# (e.g. a synos.conf copied from a UEFI machine onto BIOS hardware).
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

# syslinux can't read an encrypted partition and mbr-syslinux has no
# separate boot partition to fall back on — encrypted BIOS/MBR installs
# need mbr-grub instead.
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
  PartitionStrat VolumeStrat FilesystemStrat PackageProfile \
  Encryption UseLvm EnableSsh \
  VgName LvRootName LvSwapName SwapSize \
  ZramPercent ZramMaxMiB \
  BootFs RootFs \
  RootMountLocation BootMountLocation \
  LuksCipher LuksKeySize LuksPbkdf LuksLabel LuksPassphrase \
  KernelOpts \
  RequireWipeConfirm