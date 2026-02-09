#!/bin/zsh

# =============================================================================
#                             SYN-OS Stage 0 Script
#
# Purpose:
#   Stage 0 runs in the live environment (pre-chroot). It prepares disks,
#   mounts target filesystems, configures basic environment, installs packages
#   into the target root, and then chroots to Stage 1.
#
# Source layout and where to edit things:
#   - Disk vars are sourced from:
#       /root/syn-resources/scripts/syn-disk-config.zsh
#     Edit that file to change DISK, partition sizing, mount points, and FS types.
#
#   - Package arrays are sourced from:
#       /root/syn-resources/scripts/syn-packages.zsh
#     Edit that file to add or remove packages. Bootloader packages are appended
#     here based on firmware detection, not inside syn-packages.zsh.
#
# Advisory:
#   Keep config files limited to simple assignments. Putting commands or logic
#   inside config files can cause side effects when sourced and may break flow.
#
# Meta:
#   SYN-OS      : The Syntax Operating System
#   Author      : William Hayward-Holland (Syntax990)
#   License     : MIT License
# =============================================================================

clear

# -----------------------------------------------------------------------------#
# Disk configuration (logic-free inputs)
# -----------------------------------------------------------------------------#
DISK_CONFIG_FILE="/root/syn-resources/scripts/syn-disk-config.zsh"
if [ -f "$DISK_CONFIG_FILE" ]; then
  # shellcheck disable=SC1090
  source "$DISK_CONFIG_FILE"
else
  echo "ERROR: Disk config file missing at $DISK_CONFIG_FILE"
  exit 1
fi

# -----------------------------------------------------------------------------#
# Firmware detection
# -----------------------------------------------------------------------------#
if [ -d "/sys/firmware/efi/efivars" ]; then
  SYNOS_ENV="UEFI"
  echo "Detected UEFI system."
else
  SYNOS_ENV="MBR"
  echo "Detected MBR (BIOS) system."
fi
export SYNOS_ENV

# -----------------------------------------------------------------------------#
# Packages
# -----------------------------------------------------------------------------#
# Package arrays: coreSystem, services, environmentShell, userApplications,
# developerTools, fontsLocalization, optionalFeatures, SYNSTALL
source /root/syn-resources/scripts/syn-packages.zsh

# -----------------------------------------------------------------------------#
# Helpers
# -----------------------------------------------------------------------------#
check_success() {
  if [ $? -ne 0 ]; then
    printf "\033[1;31mError: %s\033[0m\n" "$1"
    exit 1
  fi
}

# -----------------------------------------------------------------------------#
# Aesthetics
# -----------------------------------------------------------------------------#
face() {
  clear
  echo ""
  echo "(((((((((((((((((((((((((/((((((((/***//////////////////////////////////////////"
  echo "(((((((((((((((((((((((((/**(((/*******/////////////////////////////////////////"
  echo "((((((((((((((((((((((((((***,,,,,,,,,,,,,,,,*****//////////////////////////////"
  echo "((((((((((((((((((/********,,,,,,,,,,,,,,,,,,,,,,**/////////////////////////////"
  echo "(((((((((((((((/****,,**,**,,,,,,,,,,,,,,,,,,,,,,,,****/////////////////////////"
  echo "((((((((((((/****,,,,,,,,,,,,,,,,*,,,,,,,,,,,,,,,********///////////////////////"
  echo "((((((((((**********,,,*/******,,,,*,,,,*,,,,,,,,,,,,**/////////////////////////"
  echo "(((((((((********,,,**///(((((((((//**,,,,,,,,,,,,,,,,,,*///////////////////////"
  echo "(((((((((******,,,,*/((((((((((//(//*****,,,,,,,,,,,,,,***/**/////////////////////"
  echo "(((((((//****,,,,,,/((((((((((//(*****,,,,,,,,,,,,,,***/**/////////////////////"
  echo "((((((((/****,,,***(((((((/*****(*********,,,,,,,,,,,,,,,*//////////////////////"
  echo "(((((((((/********(((((/###(((/****//(/(//*******,,,,,,,,**/////////////////////"
  echo "(((((((((********((((((((*,..*(/(/**/(*((***,,,,,*,,,,,,,,,***//////////////////"
  echo "(((((((((((******(((((///(((//***/////*,....,.,**/,,,,*/////////////////////////"
  echo "((((((((((((/(***((((((/////////////*****,******/*,,,*//////////////////////////"
  echo "((((((((((((/**/*((((((((///////(((((//*//******/*,,**//////////////////////////"
  echo "((((((((((((/*((*(((##(((/((((((///////**////***/**/////////////////////////////"
  echo "((((((((((((/*/***(((((((((((////,,(((/****//////,//////////////////////////////"
  echo "((((((((((((((/*****/(((((((((//*,,,,,,,***//////*///////////*//////////////////"
  echo "((((((((((((((********(((((***,,,,,,,,,,,,**///////////////****/////////////////"
  echo "((((((((((((((**********/(/**/////*****,,*******///////////****//////////////***"
  echo "(((((((((((((((*,,,,,,****,////////,,,,****,*,,////////////*****/*////////******"
  echo "(((((((((((((((//*,,,,,,,,,,***,****,,,,,,,******************//***************"
  echo "*,,,,,***********,****************************************/****//***************"
  echo "*,,,,,***********,****************************************/****//***************"
  echo ",,,,,,,,,,*********,,,,,,,,,,***,****,,,,,,,******************//***************"
  echo ""
  echo "Without constraints; SYN-OS has independent freedom and creative intelligence."
  echo ""
  sleep 0.2
  clear
}

wipe_art_montage() {
  echo "\033[0;31m____    __    ____  __  .______    __  .__   __.   _______ \033[0m"
  echo "\033[0;31m\\   \\  /  \\  /   / |  | |   _  \\  |  | |  \\ |  |  /  _____|\033[0m"
  echo "\033[0;31m \\   \\/    \\/   /  |  | |  |_)  | |  | |   \\|  | |  |  __  \033[0m"
  echo "\033[0;31m  \\            /   |  | |   ___/  |  | |  .    | |  | |_ | \033[0m"
  echo "\033[0;31m   \\    /\\    /    |  | |  |      |  | |  |\\   | |  |__| | \033[0m"
  echo "\033[0;31m    \\__/  \\__/     |__| | _|      |__| |__| \\__|  \\______| \033[0m"
  echo ""
  echo "\033[1;31mIf you did not verify the target, you may wipe the wrong disk.\033[0m"
  echo "Press CTRL+C to abort."
  sleep 3
}

art_montage() {
  clear
  printf "\e[1;31m~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
  printf "     _______.____    ____ .__   __.          ______        _______.\n"
  printf "    /       |\\   \\  /   / |  \\ |  |         /  __  \\      /       |\n"
  printf "   |   (----  \\   \\/   /  |   \\|  |  ______|  |  |  |    |   (---- \n"
  printf "    \\   \\      \\_    _/   |  .    | |______|  |  |  |     \\   \\    \033[0m\n"
  printf "\033[0;31m.----)   |       |  |     |  |\\   |        |   --'  | .----)   |   \033[0m\n"
  printf "\033[0;31m|_______/        |__|     |__| \\__|         \\______/  |_______/    \033[0m\n"
  printf "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\e[0m\n\n"
  sleep 1
  echo "SYN-OS Stage 0 running in pre-chroot context."
  sleep 1
  clear
}

# -----------------------------------------------------------------------------#
# Environment prep
# -----------------------------------------------------------------------------#
syn_os_environment_prep() {
  echo "Setting keyboard layout to UK"
  loadkeys uk
  check_success "Failed to set keyboard layout"
  # timedatectl set-ntp true
  # check_success "Failed to set NTP"
  # echo "Starting DHCP client..."
  # systemctl start dhcpcd.service
  # check_success "Failed to start DHCP service"
}

# -----------------------------------------------------------------------------#
# Disk processing and mount
# -----------------------------------------------------------------------------#
disk_processing() {
  # Requires: DISK, BOOT_SIZE, BOOT_FS, ROOT_FS,
  #           ROOT_MOUNT_LOCATION, BOOT_MOUNT_LOCATION, REQUIRE_WIPE_CONFIRM
  # Requires: SYNOS_ENV="UEFI"|"MBR"
  # Exports : BOOT_PART, ROOT_PART

  # Validate disk
  if [ -z "${DISK:-}" ] || [ ! -b "$DISK" ]; then
    echo "ERROR: DISK is not a valid block device (got '${DISK:-unset}')."
    exit 1
  fi

  # Safety gate
  if [ "${REQUIRE_WIPE_CONFIRM:-YES}" = "YES" ] && [ "${SYNOS_I_UNDERSTAND_WIPE:-}" != "YES" ]; then
    echo "DESTRUCTIVE: This will wipe $DISK. Set SYNOS_I_UNDERSTAND_WIPE=YES to continue."
    exit 1
  fi

  # Create ONLY the root mount dir now — boot comes after root is mounted
  mkdir -p "$ROOT_MOUNT_LOCATION"

  echo "Zeroing first MiB on ${DISK}..."
  dd if=/dev/zero of="${DISK}" bs=1M count=4 status=none || true
  sync

  if [ "$SYNOS_ENV" = "UEFI" ]; then
    echo "Creating GPT + ESP on ${DISK}..."
    parted -a optimal --script "${DISK}" \
      mklabel gpt \
      mkpart primary 1MiB "${BOOT_SIZE}" \
      set 1 esp on \
      name 1 ESP \
      mkpart primary "${BOOT_SIZE}" 100% \
      name 2 ROOT \
      print || { echo "Partitioning failed"; exit 1; }

    partprobe "$DISK" >/dev/null 2>&1 || true
    udevadm settle >/dev/null 2>&1 || sleep 1

    # Resolve by PARTLABEL (NVMe-safe)
    BOOT_PART="$(lsblk -rno PATH,PARTLABEL "${DISK}" | awk '$2=="ESP"{print $1; exit}')"
    ROOT_PART="$(lsblk -rno PATH,PARTLABEL "${DISK}" | awk '$2=="ROOT"{print $1; exit}')"

    [ -b "$BOOT_PART" ] || { echo "ESP partition not found"; exit 1; }
    [ -b "$ROOT_PART" ] || { echo "ROOT partition not found"; exit 1; }

    echo "Formatting ESP + ROOT..."
    mkfs.vfat -F 32 -n ESP "$BOOT_PART" || exit 1

    case "$ROOT_FS" in
      ext4)  mkfs.ext4  -F -L ROOT "$ROOT_PART" ;;
      f2fs)  mkfs.f2fs  -f -l ROOT "$ROOT_PART" ;;
      btrfs) mkfs.btrfs -f -L ROOT "$ROOT_PART" ;;
      xfs)   mkfs.xfs   -f -L ROOT "$ROOT_PART" ;;
      *)     echo "Unsupported ROOT_FS '$ROOT_FS'"; exit 1 ;;
    esac

  else
    echo "Creating MBR single ROOT on ${DISK}..."
    parted -a optimal --script "${DISK}" \
      mklabel msdos \
      mkpart primary 1MiB 100% \
      print || { echo "Partitioning failed"; exit 1; }

    partprobe "$DISK" >/dev/null 2>&1 || true
    udevadm settle >/dev/null 2>&1 || sleep 1

    ROOT_PART="$(lsblk -rno PATH,TYPE "${DISK}" | awk 'NR>1 && $2=="part"{print $1; exit}')"
    [ -b "$ROOT_PART" ] || { echo "ROOT partition not found"; exit 1; }

    BOOT_PART="$ROOT_PART"

    echo "Formatting ROOT..."
    case "$ROOT_FS" in
      ext4)  mkfs.ext4  -F -L ROOT "$ROOT_PART" ;;
      f2fs)  mkfs.f2fs  -f -l ROOT "$ROOT_PART" ;;
      btrfs) mkfs.btrfs -f -L ROOT "$ROOT_PART" ;;
      xfs)   mkfs.xfs   -f -L ROOT "$ROOT_PART" ;;
      *)     echo "Unsupported ROOT_FS '$ROOT_FS'"; exit 1 ;;
    esac
  fi

  echo "Mounting ROOT at ${ROOT_MOUNT_LOCATION}..."
  mount "$ROOT_PART" "$ROOT_MOUNT_LOCATION" || { echo "Failed to mount ROOT"; exit 1; }

  # NOW create boot dir inside the mounted root
  mkdir -p "$BOOT_MOUNT_LOCATION"

  if [ "$SYNOS_ENV" = "UEFI" ]; then
    echo "Mounting ESP at ${BOOT_MOUNT_LOCATION}..."
    mount "$BOOT_PART" "$BOOT_MOUNT_LOCATION" || { echo "Failed to mount ESP"; exit 1; }
  fi

  export BOOT_PART ROOT_PART ROOT_MOUNT_LOCATION BOOT_MOUNT_LOCATION

  echo "DONE:"
  echo "  ROOT = ${ROOT_PART} -> ${ROOT_MOUNT_LOCATION}"
  [ "$SYNOS_ENV" = "UEFI" ] && echo "  ESP  = ${BOOT_PART} -> ${BOOT_MOUNT_LOCATION}"
}
# -----------------------------------------------------------------------------#
# Pacstrap, mirrors, keyring, and bootloader selection
# -----------------------------------------------------------------------------#
pacstrap_sync() {
  # Refresh mirrors (requires reflector in the live env)
  echo "Refreshing mirrors..."
  reflector -c GB -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist

  # PGP keyring bootstrap (live env) so -K can copy it into the target
  echo "PGP keyring: initialise and populate"
  cat <<'EOF'
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢰⣶⣶⣶⣶⣶⣶⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⣶⣶⣶⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⣶⣶⣶⣶⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣶⣶⣶⡄⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⠿⠿⠿⠿⠿⠿⠿⠿⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠿⠿⠿⠿⠿⠿⠿⢿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⡿⠿⠀⠀⠀⠀⠀⠀⠀⠀⠸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀
⠀⠀⠀⠀⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⡇⠀⠀⠀⠀
⣀⣀⣀⣀⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣷⣶⣶⣶⡄⢸⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣷⣶⣶⣶⡆⢸⣿⣿⣿⣿⣧⣤⣤⣄⣀
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⡇⢸⣿⣿⣿⣿⣿⣿⣿⣿⠃⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⡇⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⡇⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⠿⠛⠛⠛⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⢿⣿⣿⡿⠛⠛⠛⠃⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⢰⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣶⣶⣶⣶⣶⣶⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣶⣶⣶⣶⣶⣶⣶⣾⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠿⠿⠿⠿⠿⠿⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠿⠿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡏⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⣿⣿⣿⣿⡏⠀⠀⠸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⠇⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿
⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⡇⠀⠀⠀⠀⠀⠀⢸⣿⣿⣿⣿⣿⣿⣿⣿⣿⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠸⣿⣿⣿⣿⣿
EOF
  pacman-key --init
  pacman-key --populate archlinux
  pacman -Sy
  sleep 1

  echo "Installing packages into target root..."
  # A little Pac-Man snack while pacstrap lines up packages
  cat <<'EOF'
⠀⠀⠀⠀⣀⣤⣴⣶⣶⣶⣦⣤⣀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⣠⣾⣿⣿⣿⣿⣿⣿⢿⣿⣿⣷⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀
⢀⣾⣿⣿⣿⣿⣿⣿⣿⣅⢀⣽⣿⣿⡿⠃⠀⠀⠀⠀⠀⠀⠀⠀
⣼⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠛⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⣿⣿⣿⣿⣿⣿⣿⣿⣿⠛⠁⠀⠀⣴⣶⡄⠀⣴⣶⡄⠀⣴⣶⡄
⣿⣿⣿⣿⣿⣿⣿⣿⣿⣷⣦⣀⠀⠙⠛⠁⠀⠙⠛⠁⠀⠙⠛⠁
⠸⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣦⣄⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠙⢿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⣿⡿⠃⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠈⠙⠿⣿⣿⣿⣿⣿⣿⣿⠿⠋⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
⠀⠀⠀⠀⠀⠀⠉⠉⠉⠉⠁⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀⠀
EOF
  sleep 1

  if [ "$SYNOS_ENV" = "UEFI" ]; then
    bootloaderPackages=(efibootmgr systemd)
  else
    bootloaderPackages=(syslinux)
  fi

  SYNSTALL+=("${bootloaderPackages[@]}")

  # -K copies the live keyring into the target so pacman works straight away
  pacstrap -K "$ROOT_MOUNT_LOCATION" "${SYNSTALL[@]}"
  check_success "Pacstrap failed"
}

# -----------------------------------------------------------------------------#
# Files into chroot
# -----------------------------------------------------------------------------#
dotfiles_and_vars() {
  echo "Generating fstab..."
  genfstab -U "$ROOT_MOUNT_LOCATION" >> "$ROOT_MOUNT_LOCATION/etc/fstab"
  check_success "genfstab failed"

  echo "Copying dotfiles overlay..."
  cp -Rv /root/syn-resources/DotfileOverlay/* "$ROOT_MOUNT_LOCATION"/
  check_success "Failed to copy dotfiles"

  echo "Copying Stage scripts and shared configs into target..."
  cp -v /root/syn-resources/scripts/syn-stage0.zsh "$ROOT_MOUNT_LOCATION/syn-stage0.zsh"
  check_success "Failed to copy stage0"

  cp -v /root/syn-resources/scripts/syn-stage1.zsh "$ROOT_MOUNT_LOCATION/syn-stage1.zsh"
  check_success "Failed to copy stage1"
  chmod +x "$ROOT_MOUNT_LOCATION/syn-stage1.zsh"

  cp -v /root/syn-resources/scripts/syn-packages.zsh "$ROOT_MOUNT_LOCATION/syn-packages.zsh"
  check_success "Failed to copy package config"

  # Ensure disk config is available inside chroot if Stage 1 sources it
  mkdir -p "$ROOT_MOUNT_LOCATION/root/syn-resources/scripts"
  cp -v /root/syn-resources/scripts/syn-disk-config.zsh "$ROOT_MOUNT_LOCATION/syn-disk-config.zsh"
  check_success "Failed to copy disk config"
}

# -----------------------------------------------------------------------------#
# Stage wrap-up visuals
# -----------------------------------------------------------------------------#
end_art() {
  clear
  echo ""
  printf "\033[32mSUMMARY: Stage 0 complete. Proceeding to Stage 1.\033[0m\n\n"
  printf "\033[32m• Root: %s mounted at %s\033[0m\n" "$ROOT_PART" "$ROOT_MOUNT_LOCATION"
  if [ "$SYNOS_ENV" = "UEFI" ]; then
    printf "\033[32m• Boot: %s mounted at %s (fs=%s)\033[0m\n" "$BOOT_PART" "$BOOT_MOUNT_LOCATION" "$BOOT_FS"
  fi
  printf "\033[32m• Root FS: %s\033[0m\n" "$ROOT_FS"
  printf "\033[32m• fstab generated, packages installed, scripts copied.\033[0m\n\n"
  sleep 2
}

# -----------------------------------------------------------------------------#
# Execution order
# -----------------------------------------------------------------------------#
syn_os_environment_prep
wipe_art_montage
disk_processing
face
art_montage
pacstrap_sync
face
dotfiles_and_vars
end_art

# -----------------------------------------------------------------------------#
# Enter chroot and run Stage 1
# -----------------------------------------------------------------------------#
echo "Entering chroot to execute Stage 1 with SYNOS_ENV=$SYNOS_ENV..."
arch-chroot "$ROOT_MOUNT_LOCATION" /bin/zsh -c "SYNOS_ENV=$SYNOS_ENV /syn-stage1.zsh"
check_success "Failed to execute Stage 1 inside chroot"