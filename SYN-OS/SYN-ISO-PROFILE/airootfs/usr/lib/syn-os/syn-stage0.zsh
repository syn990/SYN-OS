#!/bin/zsh
# SYN‑OS Stage 0: Orchestrator (calls modular strategy scripts)
# /usr/lib/syn-os/syn-stage0.zsh

set -euo pipefail

# Load config + packages + UI
source /usr/lib/syn-os/syn-config.zsh
source /usr/lib/syn-os/syn-packages.zsh
source /usr/lib/syn-os/ui.zsh

# Load modular strategy scripts
source /usr/lib/syn-os/syn-partition.zsh
source /usr/lib/syn-os/syn-volume.zsh
source /usr/lib/syn-os/syn-filesystem.zsh
source /usr/lib/syn-os/syn-mount.zsh
source /usr/lib/syn-os/syn-pacstrap.zsh

# Safety gate
if [ "${RequireWipeConfirm:-yes}" = "yes" ] && [ "${SynosIUnderstandWipe:-}" != "yes" ]; then
  syn_ui::wipe_warning
  echo "DESTRUCTIVE: Set SynosIUnderstandWipe=yes to continue."
  exit 1
fi

# UI
syn_ui::face
loadkeys "${KeyMap}" || true
syn_ui::intro_montage

# Execute pipeline
echo "Stage 0 starting: ${PartitionStrat} + ${VolumeStrat} + ${FilesystemStrat}…"
partitionMain
volumeMain
filesystemMain
mountMain
pacstrapMain

# Summary
syn_ui::end_summary "${RootPart}" "${RootMountLocation}" "${BootPart:-}" "${BootMountLocation}" "${BootFs}" "${RootFs}" "${PartitionStrat}"

echo "Mounts:"
mount | grep -E "${RootMountLocation}|${BootMountLocation}" || echo "(none)"
lsblk -o NAME,TYPE,FSTYPE,LABEL,PATH,MOUNTPOINTS | sed 's/^/  /'

echo "Entering chroot to Stage 1…"
arch-chroot "$RootMountLocation" /bin/zsh /usr/lib/syn-os/syn-stage1.zsh
