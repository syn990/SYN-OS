# synos.conf: Declarative Strategy Selection

`/etc/syn-os/synos.conf` is the single input to the installer. There is no interactive wizard: you edit this file with a text editor in the live environment (`nano /etc/syn-os/synos.conf`) before running `synos-install`, and every downstream decision (partitioning, encryption, filesystem, bootloader) is derived from it.

It's loaded and validated by `syn-config.zsh` (`SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/syn-config.zsh`), which fails fast (`set -euo pipefail` plus explicit `: "${Var:?message}"` checks) if a required field is missing or a strategy value isn't recognized, rather than letting a bad config reach the partitioning step.

## Fields

**System identity**
| Key | Meaning |
|---|---|
| `Hostname` | System hostname |
| `UserAccountName` | The user account Stage 1 creates and adds to `wheel` |
| `UserShell` | Login shell for that account (default `/bin/zsh`) |

**Locale / input / time**
| Key | Meaning |
|---|---|
| `Locale` | e.g. `en_GB.UTF-8`, written to `/etc/locale.conf` |
| `LocaleGen` | Line(s) written to `/etc/locale.gen` before `locale-gen` runs |
| `KeyMap` | Console keymap, also used for `loadkeys` in the live environment and `vconsole.keymap=` on the kernel cmdline |
| `TimeZone` | Path under `/usr/share/zoneinfo`; falls back to `Europe/London` if invalid |
| `VconsoleFont` | Console font written to `/etc/vconsole.conf` |

**Disk / firmware**
| Key | Meaning |
|---|---|
| `Disk` | Target block device (e.g. `/dev/vda`). Must exist: `syn-config.zsh` checks `[ -b "$Disk" ]` |
| `BootMode` | `auto` (detect via `/sys/firmware/efi/efivars`), `uefi`, or `mbr`/`bios`/`legacy` |
| `BootSize` | ESP size for UEFI installs (default `512MiB`) |

**Strategy selectors**: these are the ones that actually branch installer logic; see [Storage Strategies](./storage-strategies.md) for what each does mechanically:
| Key | Values |
|---|---|
| `PartitionStrat` | `auto` (default, recommended) \| `uefi-bootctl` \| `mbr-syslinux` \| `mbr-grub` |
| `Encryption` | `yes` \| `no`: whole-disk LUKS2 on root. **Must be `no` if `PartitionStrat=mbr-syslinux`**, since syslinux has no LUKS support; use `mbr-grub` for encrypted BIOS/MBR installs. |
| `UseLvm` | `yes` \| `no`: LVM on top of root (or on top of the LUKS mapper, if `Encryption=yes` too) |
| `FilesystemStrat` | `ext4` \| `f2fs` \| `btrfs` \| `xfs` |
| `BootloaderStrat` | `auto` \| `systemd-boot` \| `syslinux` \| `grub` |
| `PackageProfile` | `full` (default): the whole `SYNSTALL` array in `syn-packages.zsh`. `minimal`: `SYNMINIMAL` — base system, networking, shell tools, and the desktop stack only, skipping `devToolkit` and `appsMedia`'s heavier packages. Same install pipeline either way; `syn-pacstrap.zsh` is the only place the two diverge. |

`Encryption`/`UseLvm` are combined internally by `syn-config.zsh` into the `VolumeStrat` value (`luks-lvm`/`luks-only`/`lvm-only`/`plain`) that `syn-volume.zsh` actually dispatches on; you don't set `VolumeStrat` directly.

### `PartitionStrat=auto`: how it resolves, and why it matters

`syn-config.zsh` detects real firmware (`/sys/firmware/efi/efivars` present → UEFI, absent → BIOS/legacy) via the same logic that already sets `BootMode`/`SynosEnv`. When `PartitionStrat=auto` (the default), it resolves to:

| Detected firmware | `Encryption` | Resolves to |
|---|---|---|
| UEFI | either | `uefi-bootctl` |
| BIOS/legacy | `no` | `mbr-syslinux` |
| BIOS/legacy | `yes` | `mbr-grub` |

This matters because `PartitionStrat` used to be a static value with **no connection at all** to actual firmware. The shipped config could say `uefi-bootctl` while running on real legacy BIOS hardware, and nothing caught it: `parted` doesn't care what firmware it's run on and will happily write a GPT+ESP layout regardless, and `bootctl install`'s automatic `--graceful` behavior when run inside a chroot (which Stage 1 always does) means the mismatch might not even surface as an error, just an unbootable disk discovered at the next reboot. `PartitionStrat=auto` removes that trap for the common case.

If you set `PartitionStrat` explicitly (`uefi-bootctl`, `mbr-syslinux`, or `mbr-grub`), `syn-config.zsh` now cross-checks it against detected firmware and **refuses to proceed on a mismatch**: e.g. `uefi-bootctl` set on a machine that actually booted BIOS/legacy is a hard config-load error, not a silent build-then-fail. Set an explicit value only when you have a specific reason to override detection (e.g. testing, or firmware detection being wrong for unusual hardware).

**Filesystems**: `BootFs` and `RootFs` are descriptive/matching fields (`BootFs` should be `fat32` for `uefi-bootctl`'s ESP, `ext4` for `mbr-grub`'s boot partition; `RootFs` should match `FilesystemStrat`); the actual `mkfs` calls in `syn-filesystem.zsh` branch on `FilesystemStrat`, not these two directly. `volumeMain` in `syn-volume.zsh` is what actually formats the boot partition, branching on `PartitionStrat` rather than reading `BootFs`.

**LUKS** (required if `Encryption=yes`): `LuksCipher`, `LuksKeySize`, `LuksPbkdf`, `LuksLabel`, passed straight through to `cryptsetup luksFormat`.

**LVM** (required if `UseLvm=yes`): `VgName`, `LvRootName`, `LvSwapName`, `SwapSize` (`"0"` disables swap entirely, so no swap LV is created).

**Mount points**: `RootMountLocation` (default `/mnt`), `BootMountLocation` (must be a subpath of `RootMountLocation`, enforced by the config loader).

**Bootloader tuning**: `KernelOpts`, appended verbatim to the kernel command line (default `quiet splash`).

**Safety**: `RequireWipeConfirm`, when `yes` (the default, not meant to be turned off casually), makes Stage 0 interactively ask "Proceed and wipe `<disk>`? [y/N]" before touching the disk; anything but an explicit `y`/`yes` aborts. Set to `no` only for fully unattended/scripted installs. This is the only manual confirmation gate in the entire pipeline; see [How the Installer Works](./installer-overview.md).

## Note: `BootloaderStrat` vs `PartitionStrat`

The actual bootloader-install step in `syn-stage1.zsh` is keyed off `PartitionStrat`, not `BootloaderStrat`. `BootloaderStrat` only controls which package(s) `syn-pacstrap.zsh` installs (`auto` picks the right one for your `PartitionStrat` automatically: `efibootmgr`+`systemd` for `uefi-bootctl`, `grub` for `mbr-grub`, `syslinux` otherwise). Leave `BootloaderStrat=auto` unless you have a specific reason to force a particular bootloader package selection independent of `PartitionStrat`.

## Note: `synos.conf` only controls the installed system's bootloader

`BootloaderStrat`, `KernelOpts`, and everything else in this file governs the bootloader Stage 1 writes onto the *target disk*, not how the live ISO itself boots. The ISO's own boot menu (syslinux for BIOS, systemd-boot for UEFI) is fixed at ISO build time in `SYN-ISO-PROFILE/syslinux/` and `SYN-ISO-PROFILE/efiboot/`, and isn't affected by anything in `synos.conf`. See [How the Installer Works](./installer-overview.md#booting-the-iso).
