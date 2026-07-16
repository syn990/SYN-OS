#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                           S Y N - S T A G E 0
#
#   Entry point for the SYN-OS installer: partitioning, volume management,
#   filesystem creation, mounting, and pacstrap, then hands off to Stage 1
#   inside the chroot.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-STAGE0 (Installer)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

# Load config + packages + UI scripts
source /usr/lib/syn-os/syn-config.zsh
source /usr/lib/syn-os/syn-packages.zsh
source /usr/lib/syn-os/syn-ui.zsh

# Duplicates all output (Stage 0 and, once chrooted, Stage 1) to a log
# file via `script` rather than a `tee` pipe — pacman/pacstrap check
# isatty() to decide whether to draw progress bars, and a pipe fails that
# check where a pty doesn't. Interactive prompts read /dev/tty directly so
# they're unaffected. Written to /root here (outside Stage 1's chroot) and
# copied onto the target disk after arch-chroot returns.
InstallLog="/root/synos-install-$(date +%Y%m%d-%H%M%S).log"
if [ -z "${SYN_STAGE0_UNDER_SCRIPT:-}" ]; then
  export SYN_STAGE0_UNDER_SCRIPT=1
  exec script -qefc "$0 $*" "$InstallLog"
fi
syn_ui::info "Logging full install output to ${InstallLog}"

syn_stage0::cleanup() {
  [ -n "${SynStage0Complete:-}" ] && return
  local agentDir="${RootMountLocation:-/mnt}/etc/pacman.d/gnupg"
  pkill -9 -f "gpg-agent --homedir ${agentDir}" 2>/dev/null || true
  umount -R "${RootMountLocation:-/mnt}" 2>/dev/null || true
}
trap syn_stage0::cleanup EXIT INT TERM

# Load the disk-prep pipeline (partition -> volume -> filesystem -> mount)
# and the package/deploy stage
source /usr/lib/syn-os/syn-disk.zsh
source /usr/lib/syn-os/syn-pacstrap.zsh

# Explicit confirmation before anything destructive (partitioning,
# formatting) happens. RequireWipeConfirm=no in synos.conf skips this —
# default is "yes" and it's not meant to be turned off casually.
if [ "${RequireWipeConfirm:-yes}" = "yes" ]; then
  syn_ui::confirm_wipe "${Disk}" || { syn_ui::error "Aborted — disk not confirmed."; exit 1; }
fi

# UI
syn_ui::face
loadkeys "${KeyMap}" || true
setfont "${VconsoleFont}" || true
syn_ui::intro_montage

# Execute pipeline
syn_ui::step "Stage 0: ${PartitionStrat} + ${VolumeStrat} + ${FilesystemStrat}"
partitionMain
volumeMain
filesystemMain
mountMain
pacstrapMain
syn_ui::step_done "Stage 0 pipeline complete"
SynStage0Complete=1

# Summary
syn_ui::end_summary "${RootPart}" "${RootMountLocation}" "${BootPart:-}" "${BootMountLocation}" "${BootFs}" "${RootFs}" "${PartitionStrat}"

syn_ui::info "Mounts:"
mount | grep -E "${RootMountLocation}|${BootMountLocation}" || echo "(none)"
lsblk -o NAME,TYPE,FSTYPE,LABEL,PATH,MOUNTPOINTS | sed 's/^/  /'

syn_ui::step "Entering chroot to Stage 1"
arch-chroot "$RootMountLocation" /bin/zsh /usr/lib/syn-os/syn-stage1.zsh

# Copies the full Stage 0 + Stage 1 log onto the installed disk so it
# survives past this live session.
cp -f "$InstallLog" "${RootMountLocation}/$(basename "$InstallLog")" 2>/dev/null || true
