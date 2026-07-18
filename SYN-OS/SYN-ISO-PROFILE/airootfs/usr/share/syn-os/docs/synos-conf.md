# synos.conf Reference

`/etc/syn-os/synos.conf` is the single input to the installer. There is no interactive wizard: you edit this file with a text editor in the live environment (`nano /etc/syn-os/synos.conf`) before running `syn-stage0.zsh`, and every downstream decision — partitioning, encryption, filesystem, bootloader, package set — is derived from it. It's a flat `zsh`-sourceable key=value file; every field below is a real assignment in the shipped file at `SYN-OS/SYN-ISO-PROFILE/airootfs/etc/syn-os/synos.conf`.

It's loaded, normalized, and validated by `syn-config.zsh` (`SYN-OS/SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/syn-config.zsh`), sourced by both Stage 0 and Stage 1. It fails fast — `set -euo pipefail` plus explicit `: "${Var:?message}"` checks and `case`-statement rejections — if a required field is missing or a strategy value isn't recognized, rather than letting a bad config reach the partitioning step.

## Fields

**System identity**

| Key | Meaning |
|---|---|
| `Hostname` | System hostname, required. |
| `UserAccountName` | The user account Stage 1 creates (`useradd -m -G wheel`) and builds `syn-filemanager` as. Required. |
| `UserAccountPassword` | Plaintext password, consumed once by `chpasswd` in Stage 1. Ships as `CHANGE_ME`; Stage 1 refuses to proceed if it's still set to that placeholder. Never persists on the installed disk: `syn-pacstrap.zsh`'s `pacstrapMain` copies it onto the target (Stage 1 needs it), and `syn-stage1.zsh` strips the line from `synos.conf` with `sed -i` right after `chpasswd` consumes it. |
| `UserShell` | Login shell for that account. Defaults to `/bin/zsh` if unset (`syn-config.zsh`: `: "${UserShell:=/bin/zsh}"`). |

**Locale / input / time**

| Key | Meaning |
|---|---|
| `Locale` | e.g. `en_GB.UTF-8`, written to `/etc/locale.conf` as `LANG=`. Required. |
| `LocaleGen` | Line written to `/etc/locale.gen` before `locale-gen` runs. Required. |
| `KeyMap` | Console keymap — used for `loadkeys` in the live environment, `KEYMAP=` in `/etc/vconsole.conf`, and `vconsole.keymap=` on the kernel command line. Required. |
| `TimeZone` | Path under `/usr/share/zoneinfo`; Stage 1 falls back to `Europe/London` if `/usr/share/zoneinfo/$TimeZone` doesn't exist. Required. |
| `VconsoleFont` | Console font, used for `setfont` in the live environment and `FONT=` in `/etc/vconsole.conf`. Required. |

**Disk / firmware**

| Key | Meaning |
|---|---|
| `Disk` | Target block device (e.g. `/dev/sda`, `/dev/nvme0n1`). Ships as `CHANGE_ME`; `syn-config.zsh` checks `[ -b "$Disk" ]` and hard-fails if it isn't a real block device. Check the exact name with `lsblk` before setting this — there's no other safety net besides the wipe confirmation prompt. |
| `BootMode` | `auto` (default) detects real firmware via `/sys/firmware/efi/efivars`; `uefi` or `bios`/`mbr`/`legacy` force a value. Feeds `SynosEnv` (`UEFI` or `MBR`), used only for `PartitionStrat` resolution/validation below. |
| `BootSize` | Size of the ESP (`uefi-bootctl`) or boot partition (`mbr-grub`). Default `512MiB`. |

**Strategy selectors** — the ones that actually branch installer logic; see [Storage Strategies](./storage-strategies.md) for what each does mechanically:

| Key | Values |
|---|---|
| `PartitionStrat` | `auto` (default, recommended) \| `uefi-bootctl` \| `mbr-syslinux` \| `mbr-grub` |
| `Encryption` | `yes` \| `no`: whole-disk LUKS2 on root. |
| `UseLvm` | `yes` \| `no`: LVM on top of root (or on top of the LUKS mapper, if `Encryption=yes` too). |
| `FilesystemStrat` | `ext4` \| `f2fs` \| `btrfs` \| `xfs` |
| `BootloaderStrat` | `auto` \| `systemd-boot` \| `syslinux` \| `grub` — controls only which bootloader *package(s)* `pacstrapMain` installs, see note below. |
| `PackageProfile` | `full` (default) \| `minimal` — which array in `syn-packages.zsh` `pacstrapMain` installs. See [Package Collection](./packages.md). |

`Encryption`/`UseLvm` are combined internally by `syn-config.zsh` into an internal `VolumeStrat` value (`luks-lvm` \| `luks-only` \| `lvm-only` \| `plain`) that `syn-disk.zsh`'s `volumeMain` actually dispatches on — there's no `VolumeStrat` key to set directly in `synos.conf`.

### Boolean normalization

`Encryption`, `UseLvm`, `EnableSsh`, and `RequireWipeConfirm` all pass through `syn-config.zsh`'s `toYesNo()` helper, which lowercases the value and maps `y`/`yes`/`true`/`1` to `yes`, and `n`/`no`/`false`/`0`/empty to `no` — anything else passes through unchanged (and will fail whatever check consumes it next). This means `Encryption=Y`, `Encryption=true`, and `Encryption=yes` are all equivalent in practice, though the shipped file only ever uses `yes`/`no`.

### `PartitionStrat=auto` resolution

`syn-config.zsh` detects real firmware — `/sys/firmware/efi/efivars` present means UEFI, absent means BIOS/legacy — and sets `SynosEnv` accordingly (`UEFI` or `MBR`) unless `BootMode` overrides it explicitly. When `PartitionStrat=auto` (the default), it then resolves to:

| Detected firmware (`SynosEnv`) | `Encryption` | Resolves to |
|---|---|---|
| UEFI | either | `uefi-bootctl` |
| MBR (BIOS/legacy) | `no` | `mbr-syslinux` |
| MBR (BIOS/legacy) | `yes` | `mbr-grub` |

This resolves against real detected firmware, not a hardcoded guess — the shipped `synos.conf`'s comment block spells out why: a stale `uefi-bootctl` value carried over onto real BIOS hardware would previously build silently and only fail at the next reboot, unbootable. `PartitionStrat=auto` closes that gap for the common case. The BIOS+`Encryption=yes` branch resolving to `mbr-grub` rather than `mbr-syslinux` exists specifically because syslinux has no LUKS support at all — see [Storage Strategies](./storage-strategies.md#mbr-syslinux) for the mechanical reason.

If `PartitionStrat` is set explicitly rather than left `auto`, `syn-config.zsh` still cross-checks it against detected firmware:

- `PartitionStrat=uefi-bootctl` on a machine that actually booted MBR/BIOS is a hard config-load error.
- `PartitionStrat=mbr-syslinux` or `mbr-grub` on a machine that actually booted UEFI is a hard config-load error.
- `PartitionStrat=mbr-syslinux` combined with `Encryption=yes` is a hard config-load error regardless of firmware, pointing you at `mbr-grub` instead.

Every one of these is a config-load-time failure (before Stage 0 touches the disk), not a runtime failure after partitioning has already started.

**Filesystems**: `BootFs` and `RootFs` are descriptive/documentation fields only — `BootFs` should read `fat32` for `uefi-bootctl`'s ESP or `ext4` for `mbr-grub`'s boot partition; `RootFs` should match `FilesystemStrat`. The actual `mkfs` calls in `syn-disk.zsh`'s `filesystemMain` branch on `FilesystemStrat` directly, and `volumeMain`'s boot-partition formatting branches on `PartitionStrat` directly — neither reads `BootFs`/`RootFs`.

**LUKS** (required if `Encryption=yes`, enforced by a `case "${VolumeStrat}" in luks-lvm|luks-only)` block): `LuksCipher` (default `aes-xts-plain64`), `LuksKeySize` (default `512`), `LuksPbkdf` (default `argon2id`), `LuksLabel` (default `cryptroot`, the mapper name `cryptsetup open` uses), and `LuksPassphrase` — passed straight to `cryptsetup luksFormat`/`open`. Ships as `CHANGE_ME`; `syn-config.zsh` hard-fails if it's still that placeholder and `Encryption=yes`. Stripped from the `synos.conf` copy on the target disk by `pacstrapMain` before install, since it's fully consumed by the time `pacstrapMain` runs.

**LVM** (required if `UseLvm=yes`, enforced by a `case "${VolumeStrat}" in luks-lvm|lvm-only)` block): `VgName` (default `vg0`), `LvRootName` (default `root`), `LvSwapName` (default `swap`), `SwapSize` (`"0"` disables swap entirely — no swap LV is created at all, regardless of `LvSwapName`).

**Mount points**: `RootMountLocation` (default `/mnt`, required), `BootMountLocation` (default `/mnt/boot`, required, and `syn-config.zsh` enforces it's a subpath of `RootMountLocation`).

**Bootloader tuning**: `KernelOpts` (default `quiet splash`), appended verbatim to the kernel command line Stage 1 constructs.

**Remote access**: `EnableSsh` (`yes`/`no`, default `no`) controls only whether `sshd.service` is enabled at boot on the installed system — `openssh` itself is always installed via [Package Collection](./packages.md) regardless of this setting, same as a plain Arch install where `sshd` is present but inactive until enabled.

**Safety**: `RequireWipeConfirm` (`yes`/`no`, default `yes`, not meant to be turned off casually) — when `yes`, Stage 0 interactively asks "Proceed and wipe `<disk>`? [y/N]" before touching the disk; anything but an explicit yes aborts. Set to `no` only for fully unattended/scripted installs where `Disk` has already been verified some other way. This is the only manual confirmation gate in the entire pipeline; see [How the Installer Works](./installer-overview.md).

## Validation behavior

Every check in `syn-config.zsh` runs at config-load time, immediately after sourcing `synos.conf`, before Stage 0 does anything to the disk. Failures exit nonzero with a message on stderr describing exactly what's wrong and, where relevant, what to change. There's no partial-validation mode and no way to skip a check — a missing `Hostname`, an unrecognized `FilesystemStrat`, a `CHANGE_ME` still in `Disk`/`UserAccountPassword`/`LuksPassphrase`, or a `PartitionStrat`/firmware/encryption mismatch all stop the installer cold rather than proceeding on a best-effort basis.

## Note: `BootloaderStrat` vs `PartitionStrat`

The actual bootloader-install step in `syn-stage1.zsh` is keyed off `PartitionStrat`, not `BootloaderStrat`. `BootloaderStrat` only controls which package(s) `syn-pacstrap.zsh`'s `pacstrapMain` installs during Stage 0: `auto` (the default) picks the package set matching your resolved `PartitionStrat` automatically (`efibootmgr systemd` for `uefi-bootctl`, `grub` for `mbr-grub`, `syslinux` otherwise). Leave `BootloaderStrat=auto` unless you have a specific reason to force a particular bootloader package selection independent of `PartitionStrat`.

## Note: `synos.conf` only controls the installed system's bootloader

`BootloaderStrat`, `KernelOpts`, and everything else in this file governs the bootloader Stage 1 writes onto the *target disk*, not how the live ISO itself boots. The ISO's own boot menu (syslinux for BIOS, systemd-boot for UEFI) is fixed at ISO build time and isn't affected by anything in `synos.conf`. See [How the Installer Works](./installer-overview.md#booting-the-iso).
