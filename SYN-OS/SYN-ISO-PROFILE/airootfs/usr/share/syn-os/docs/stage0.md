# Stage 0: Pre-Chroot Setup

Stage 0 (`syn-stage0.zsh`) runs entirely in the live ISO environment, before anything is chrooted. Its job is to turn a blank disk into a mounted, package-installed root filesystem, then hand off to Stage 1. Every step below runs under `set -euo pipefail`: any failure aborts the install instead of limping on with an inconsistent disk.

## Full install log

The first thing `syn-stage0.zsh` does is re-exec itself under `script -qefc`, writing a full transcript to `/root/synos-install-<timestamp>.log` (later copied onto the installed disk). This is also why pacman's progress bars render at all. Pacman checks `isatty()` on stdout to decide whether to draw them, and that check fails the moment stdout is piped through anything — even a plain `tee -a "$InstallLog"`, which an earlier version of this script used, and even with `Color`/`ILoveCandy` set in `pacman.conf`. `script` gives the child process a real pty, so `isatty()` passes and the bars render, while the full transcript still gets captured to the log file.

## 1. `partitionMain`: `syn-disk.zsh`

Zeroes the first 4 MiB of the target disk (`Disk` from `synos.conf`), then dispatches on `PartitionStrat`:

- **`uefi-bootctl`**: creates a GPT label with a FAT ESP (sized `BootSize`, default `512MiB`) and a second partition using the rest of the disk. Sets `BootPart`/`RootPart` to the resulting device nodes (handling both `/dev/sdXN` and `/dev/nvme0n1pN`-style naming via the `p1`/`1` fallback).
- **`mbr-syslinux`**: creates an MSDOS label with a single primary partition spanning the whole disk. `BootPart` and `RootPart` are the same device; there's no separate boot partition in the MBR path. Only valid with `Encryption=no` (see [synos.conf](./synos-conf.md)).
- **`mbr-grub`**: creates an MSDOS label with an ext4 boot partition (sized `BootSize`) and a second partition using the rest of the disk, same two-partition shape as `uefi-bootctl`. This is the BIOS/MBR strategy for encrypted installs; see [Storage Strategies](./storage-strategies.md#mbr-grub).

A `waitForBlock` helper polls for up to 5 seconds for the partition device node to appear before failing, since `partprobe`/`udevadm settle` don't always guarantee the kernel has caught up.

## 2. `volumeMain`: `syn-disk.zsh`

If `BootPart` differs from `RootPart` (`uefi-bootctl` or `mbr-grub`), formats the boot partition first: FAT32 for `uefi-bootctl`'s ESP, ext4 for `mbr-grub`'s boot partition. Then dispatches on the internal `VolumeStrat` (derived from `Encryption`/`UseLvm` by `syn-config.zsh`):

| `Encryption` | `UseLvm` | What happens |
|---|---|---|
| yes | yes | LUKS2-encrypts `RootPart`, opens it as `LuksLabel`, then creates an LVM PV/VG on the decrypted mapper device, with optional swap LV and a root LV using all remaining space. |
| yes | no | LUKS2-encrypts `RootPart`, opens it: the mapper device *is* the root filesystem device, no LVM layer. |
| no | yes | LVM directly on `RootPart`, no encryption. |
| no | no | `RootPart` is used as-is: no LUKS, no LVM. |

Every path sets `RootFsDev` (the block device the filesystem gets created on) and exports it, along with `SwapDev` and `LuksUuid` (empty string where not applicable) for Stage 1's state handoff.

LUKS parameters (`LuksCipher`, `LuksKeySize`, `LuksPbkdf`, `LuksLabel`) all come from `synos.conf`; see [Storage Strategies](./storage-strategies.md).

## 3. `filesystemMain`: `syn-disk.zsh`

Formats `RootFsDev` according to `FilesystemStrat` (`ext4`, `f2fs`, `btrfs`, or `xfs`), all labeled `ROOT`. If `SwapDev` was set by the volume stage, formats it as swap too.

## 4. `mountMain`: `syn-disk.zsh`

Mounts `RootFsDev` at `RootMountLocation` (default `/mnt`). If there's a separate boot partition, mounts it at `BootMountLocation` (default `/mnt/boot`). Activates swap if present.

## 5. `pacstrapMain`: `syn-pacstrap.zsh`

This is the largest step:

1. Runs `reflector` to generate a fresh mirrorlist (region `GB`, hardcoded), initializes and populates the pacman keyring, and syncs package databases.
2. Picks bootloader packages based on `BootloaderStrat`/`PartitionStrat` (`efibootmgr`+`systemd` for `uefi-bootctl`, `grub` for `mbr-grub`, `syslinux` otherwise) and appends them to the `SYNSTALL` array from `syn-packages.zsh`; see [Package Collection](./packages.md).
3. Runs `pacstrap -K` with the full `SYNSTALL` array, then `genfstab -U` to write `/etc/fstab`.
4. Copies `synos.conf` and every `*.zsh` script under `/usr/lib/syn-os/` into the target system, so Stage 1 (which runs from inside the target via chroot) has access to them. `UserAccountPassword` travels in this copy since Stage 1 needs it for `chpasswd` — the plaintext password should never live permanently on the installed disk, only pass through transiently; Stage 1 strips it from `synos.conf` right after use (see [synos.conf](./synos-conf.md#fields) and [Stage 1](./stage1.md)).
5. Deploys `DotfileOverlay/` onto the target filesystem: this is how `/etc/skel`, themes, and `/usr/local/bin` scripts get onto the installed system. See [Dotfile Overlay](./dotfile-overlay.md).
6. Copies `/usr/share/syn-os/docs` (this documentation, plus its diagrams) onto the target filesystem, so it's readable from the installed desktop later via the Docs menu, not just on GitHub. See [LabWC](./labwc.md).
7. Writes `/etc/syn-os/install.state` inside the target (`chmod 600`): a flat key=value file capturing only the values Stage 0 computed at runtime (`RootFsDev`, `LuksUuid`, `SwapDev`, `RootPart`/`BootPart`, `RootMapper`) that Stage 1 needs but can't re-derive on its own — re-running the partition/volume logic a second time would be destructive. Everything else Stage 1 needs (`Hostname`, `KeyMap`, `UserAccountPassword`, etc.) comes from the `synos.conf` copy instead, via `syn-config.zsh`.

## Handoff

Once `pacstrapMain` returns, Stage 0 prints a mount summary and runs:

```zsh
arch-chroot "$RootMountLocation" /bin/zsh /usr/lib/syn-os/syn-stage1.zsh
```

From here, control passes to [Stage 1](./stage1.md), running inside the new system.
