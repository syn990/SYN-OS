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

# Firmware mode (infer from PartitionStrat if BootMode not explicitly set)
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

# Strategy validation
case "${PartitionStrat:-}" in
  uefi-bootctl|mbr-syslinux) : ;;
  *) echo "ERROR: Unknown PartitionStrat '${PartitionStrat:-}'" >&2; exit 1 ;;
esac

case "${VolumeStrat:-}" in
  luks-lvm|luks-only|lvm-only|plain) : ;;
  *) echo "ERROR: Unknown VolumeStrat '${VolumeStrat:-}'" >&2; exit 1 ;;
esac

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
    ;;
esac

# --- export for Stage 0/1 --------------------------------
export \
  SynosEnv \
  Hostname UserAccountName UserShell \
  Locale LocaleGen KeyMap TimeZone VconsoleFont \
  Disk BootMode BootSize \
  PartitionStrat VolumeStrat FilesystemStrat BootloaderStrat \
  VgName LvRootName LvSwapName SwapSize \
  BootFs RootFs \
  RootMountLocation BootMountLocation \
  LuksCipher LuksKeySize LuksPbkdf LuksLabel \
  KernelOpts \
  RequireWipeConfirm