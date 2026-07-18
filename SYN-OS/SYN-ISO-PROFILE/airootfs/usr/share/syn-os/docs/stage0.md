# Stage 0: Pre-Chroot Setup

Stage 0 (`syn-stage0.zsh`) runs entirely in the live ISO environment, before anything is chrooted. Its job is to turn a blank disk into a mounted, package-installed root filesystem, then hand off to Stage 1. The whole script runs under `set -euo pipefail`: any failure aborts the install instead of limping on with an inconsistent disk. A trap on `EXIT INT TERM` (`syn_stage0::cleanup`) kills any leftover `gpg-agent` under the target's pacman gnupg homedir and unmounts `RootMountLocation` recursively if the script exits before `SynStage0Complete` gets set — so a failed or interrupted run doesn't leave the disk half-mounted.

## Full install log

The first thing `syn-stage0.zsh` does is check for `SYN_STAGE0_UNDER_SCRIPT` and, if unset, re-exec itself under `script -qefc "$0 $*" "$InstallLog"`, writing a full transcript to `/root/synos-install-<timestamp>.log` (later copied onto the installed disk by the very last line of the script). This is deliberately `script`, not a `tee` pipe: `pacman`/`pacstrap` check `isatty()` on stdout to decide whether to draw their progress bars, and that check fails the moment stdout is piped through anything — even `... | tee -a "$InstallLog"`. `script` gives the child process a real pty, so `isatty()` passes and the bars render, while the full transcript still gets captured to the log file. Interactive prompts (the wipe confirmation, see below) read `/dev/tty` directly, so they're unaffected by any of this.

## Wipe confirmation

Before touching the disk, if `RequireWipeConfirm=yes` (the default, and not meant to be turned off casually), `syn_ui::confirm_wipe "${Disk}"` asks for explicit confirmation; anything other than an explicit yes aborts the install with `syn_ui::error` and a nonzero exit. `RequireWipeConfirm=no` in `synos.conf` skips this entirely, for scripted/unattended installs where `Disk` has already been verified some other way. This is the only interactive gate in the whole pipeline — everything after it runs unattended and is destructive.

After confirmation, Stage 0 shows a splash (`syn_ui::face`, `syn_ui::intro_montage`), loads the configured keymap and console font (`loadkeys "${KeyMap}"`, `setfont "${VconsoleFont}"`, both best-effort), and starts the pipeline.

## The pipeline

```zsh
partitionMain
volumeMain
filesystemMain
mountMain
pacstrapMain
```

All five functions are sourced from `syn-disk.zsh` (the first four) and `syn-pacstrap.zsh` (the last), both sourced near the top of `syn-stage0.zsh` alongside `syn-config.zsh`, `syn-packages.zsh`, and `syn-ui.zsh`.

### 1. `partitionMain` (`syn-disk.zsh`)

First calls `clearDiskHolders "${Disk}"`, which turns off swap and, for every existing partition on the disk, recursively closes any LVM VG or open LUKS mapper sitting on top of it (`closeHolder`, walking `/sys/class/block/*/holders/*` and falling back to `dmsetup remove` for orphaned device-mapper targets) — this matters on a disk reused from a previous install. Then zeroes the first 4 MiB of `Disk` (`dd if=/dev/zero ... bs=1M count=4`) to clear stale partition-table signatures, and dispatches on `PartitionStrat`:

- **`uefi-bootctl`**: `parted` creates a GPT label with a FAT ESP (`mkpart primary 1MiB "${BootSize}" name 1 ESP set 1 esp on`) and a second partition using the rest of the disk. `BootPart`/`RootPart` are resolved with a `${Disk}p1`-then-`${Disk}1` fallback so this works on both `/dev/sdXN` and `/dev/nvme0n1pN` naming.
- **`mbr-syslinux`**: `parted` creates an MSDOS label with one primary partition spanning the whole disk. `BootPart` is set equal to `RootPart` — there is no separate boot partition in this strategy.
- **`mbr-grub`**: `parted` creates an MSDOS label with an ext4-flagged boot partition (sized `BootSize`) and a second partition using the rest of the disk — the same two-partition shape as `uefi-bootctl`, just MBR. See [Storage Strategies](./storage-strategies.md#mbr-grub) for why this strategy specifically needs a separate boot partition where `mbr-syslinux` doesn't.

A `waitForBlock` helper polls up to 50 times at 0.1s intervals (5 seconds total) for each partition's device node to appear before failing, since `partprobe`/`udevadm settle` don't always guarantee the kernel has caught up immediately. After the strategy-specific function returns, `partitionMain` runs `wipefs -a` on every partition it just created — the earlier 4 MiB zero only clears the partition-table header, not a filesystem/LUKS/LVM signature further into a reused partition, and a stale signature there could otherwise confuse a later `mount`/probe.

### 2. `volumeMain` (`syn-disk.zsh`)

If `BootPart` differs from `RootPart` (`uefi-bootctl` or `mbr-grub`), formats the boot partition first: `mkfs.vfat -F32 -n ESP` for `uefi-bootctl`'s ESP, `mkfs.ext4 -F -L BOOT` for `mbr-grub`'s boot partition. Then dispatches on the internal `VolumeStrat` value `syn-config.zsh` derived from `Encryption`/`UseLvm`:

| `VolumeStrat` | Function | What happens |
|---|---|---|
| `luks-lvm` | `volumeStrat_luks_lvm` | LUKS2-formats `RootPart`, opens it as `LuksLabel`, creates an LVM PV/VG (`VgName`) on the decrypted mapper, optional swap LV, root LV takes `100%FREE`. |
| `luks-only` | `volumeStrat_luks_only` | LUKS2-formats and opens `RootPart`; the mapper device itself becomes `RootFsDev`, no LVM layer. |
| `lvm-only` | `volumeStrat_lvm_only` | LVM PV/VG directly on `RootPart`, no encryption. |
| `plain` | `volumeStrat_plain` | `RootFsDev` points straight at `RootPart` — no LUKS, no LVM. |

Every path exports `RootMapper`, `RootFsDev`, `SwapDev`, and `LuksUuid` (the latter two empty strings where not applicable) for `pacstrapMain`'s state handoff at the end of Stage 0. LUKS parameters (`LuksCipher`, `LuksKeySize`, `LuksPbkdf`, `LuksLabel`, `LuksPassphrase`) all come from `synos.conf`; see [Storage Strategies](./storage-strategies.md) for the full field reference.

### 3. `filesystemMain` (`syn-disk.zsh`)

Loads the kernel module for `FilesystemStrat` (`modprobe`, best-effort — `mkfs.*` only needs the userspace tool, but `mount`'s auto-detection later needs the module already loaded, and it isn't guaranteed to autoload on the live image), then formats `RootFsDev`, all labeled `ROOT`:

```
ext4  -> mkfs.ext4 -F -L ROOT
f2fs  -> mkfs.f2fs -f -l ROOT
btrfs -> mkfs.btrfs -f -L ROOT
xfs   -> mkfs.xfs -f -L ROOT
```

If `SwapDev` was set by the volume stage, formats it too (`mkswap -L SWAP`).

### 4. `mountMain` (`syn-disk.zsh`)

Mounts `RootFsDev` at `RootMountLocation` (default `/mnt`). If there's a separate boot partition (`BootPart` differs from `RootPart`), mounts it at `BootMountLocation` (default `/mnt/boot`). Activates swap with `swapon` if `SwapDev` is set.

### 5. `pacstrapMain` (`syn-pacstrap.zsh`)

The largest step, and the one that actually populates the target filesystem:

1. Runs `reflector -c GB -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist` to generate a fresh mirrorlist, then `pacman-key --init`, `pacman-key --populate archlinux`, and `pacman -Sy`.
2. Picks bootloader packages based on `PartitionStrat`: `uefi-bootctl` → `efibootmgr` (systemd-boot itself is already in `baseCore`), `mbr-grub` → `grub`, anything else → `syslinux`.
3. Picks the package array based on `PackageProfile`: `minimal` uses `SYNMINIMAL`, anything else (including the default `full`) uses `SYNSTALL`. Appends the bootloader packages from step 2. See [Package Collection](./packages.md) for both arrays in full.
4. Runs `pacstrap -K "${RootMountLocation}" "${packageList[@]}"`, then `genfstab -U "${RootMountLocation}" >> "${RootMountLocation}/etc/fstab"`.
5. Strips `LuksPassphrase` from `/etc/syn-os/synos.conf` (it's already fully consumed by `cryptsetup` earlier in this same run — it never needs to reach the target disk), then copies the resulting `synos.conf` and every `*.zsh` script under `/usr/lib/syn-os/` onto the target, so Stage 1 has them available inside the chroot. `UserAccountPassword` still travels in this copy — Stage 1 needs it for `chpasswd` and strips it itself right after (see [Stage 1](./stage1.md)).
6. Deploys `DotfileOverlay/` onto the target filesystem if present (`cp -r`), then explicitly re-applies the executable bit on `/usr/lib/syn-os`, `/usr/local/bin`, and the `labwc`/`waybar`/`superfile` skel config directories — a plain file copy doesn't always preserve it. See [Dotfile Overlay](./dotfile-overlay.md).
7. Copies `/usr/lib/syn-os/syn-filemanager-src` onto the target as `/usr/src/syn-filemanager` if present — the source `syn-filemanager` builds from inside the chroot in Stage 1, not a prebuilt binary shipped here.
8. Copies `/usr/share/syn-os/docs` (this documentation, plus diagrams) onto the target if present, so it's readable from the installed desktop's Docs menu, not just from the live ISO or GitHub.
9. Writes `/etc/syn-os/install.state` on the target (`chmod 600`): a flat key=value file with exactly the values Stage 0 computed at runtime and that Stage 1 can't safely re-derive — `BootPart`, `RootPart`, `RootMapper`, `RootFsDev`, `SwapDev`, `LuksUuid`. Everything else Stage 1 needs (`Hostname`, `KeyMap`, `UserAccountPassword`, etc.) comes from the `synos.conf` copy from step 5, via `syn-config.zsh`.

## Handoff

Once `pacstrapMain` returns, Stage 0 prints a summary of what's mounted where (`syn_ui::end_summary`, then `mount`/`lsblk` output) and runs:

```zsh
arch-chroot "$RootMountLocation" /bin/zsh /usr/lib/syn-os/syn-stage1.zsh
```

`SynStage0Complete=1` is set immediately before this line, so the `EXIT` trap's cleanup (killing the gnupg agent, unmounting `RootMountLocation`) is skipped once the pipeline has actually succeeded — the chroot needs those mounts to stay in place. When `arch-chroot` returns, Stage 0's very last action is copying the full install log onto the installed disk. From here, control has already passed to [Stage 1](./stage1.md), running inside the new system.
