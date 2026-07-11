#!/bin/zsh
# SYN‑OS Stage 0: Orchestrator (Partitioning, Volume Management, Filesystem Creation, Mounting, and Pacstrap)
# This is the entry point for the SYN‑OS installation process. It sets up the environment, loads modular scripts for each stage, and executes them in sequence.

# /usr/lib/syn-os/syn-stage0.zsh

set -euo pipefail

# Load config + packages + UI scripts
source /usr/lib/syn-os/syn-config.zsh
source /usr/lib/syn-os/syn-packages.zsh
source /usr/lib/syn-os/ui.zsh

# Full install log — everything printed from here on (Stage 0's own output,
# plus Stage 1's once chrooted, since arch-chroot execs it as a child process
# inheriting this same fd) is duplicated to a logfile as well as the
# terminal. Doesn't interfere with interactive prompts (passwd, the wipe
# confirm below) since those read from /dev/tty explicitly, independent of
# stdout/stderr. Written to the live environment's /root (Stage 1's chroot
# can't reach outside itself to write there), then copied onto the target
# disk's / after arch-chroot returns, so it survives past this live session
# instead of vanishing with tmpfs at reboot.
#
# Uses `script` (util-linux, always present — it's a base dependency), not a
# plain `tee` pipe, to capture this: `exec > >(tee ...)` replaces stdout with
# a pipe, and pacman/pacstrap check isatty() on stdout to decide whether to
# draw ILoveCandy/progress bars — a pipe fails that check even though
# Color/ILoveCandy are enabled in pacman.conf, so every install rendered
# bare, no bars, looking stalled during the ~1.6GB package download. `script`
# allocates a real pty, so isatty() still says yes, while still writing the
# full transcript (bars, colors, redraws and all) to the log file.
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

# Load modular strategy scripts
source /usr/lib/syn-os/syn-partition.zsh
source /usr/lib/syn-os/syn-volume.zsh
source /usr/lib/syn-os/syn-filesystem.zsh
source /usr/lib/syn-os/syn-mount.zsh
source /usr/lib/syn-os/syn-pacstrap.zsh

# Safety gate to stop idiots loosing all their data. Since SYN‑OS operates at
# a very low level (partitioning and formatting disks), an explicit
# interactive confirmation is required before anything destructive happens.
# Set RequireWipeConfirm=no in synos.conf to skip this — default is "yes"
# and this is not meant to be turned off casually.
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

# Copy the full install log (Stage 0 + Stage 1 output — Stage 1 inherited
# this same tee'd fd across arch-chroot) onto the installed system's own /,
# so it survives past this live session instead of vanishing with tmpfs.
cp -f "$InstallLog" "${RootMountLocation}/$(basename "$InstallLog")" 2>/dev/null || true
