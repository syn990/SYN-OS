# Storage Strategies

SYN-OS treats "how is the disk laid out" (`PartitionStrat`), "is root encrypted / on LVM" (`Encryption`/`UseLvm`), and "what filesystem is on root" (`FilesystemStrat`) as three independent config keys in [synos.conf](./synos-conf.md), each handled by its own dispatcher function in `syn-disk.zsh` (`SYN-OS/SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/syn-disk.zsh`). Changing encryption on/off or switching filesystem never touches partitioning code, and vice versa.

## Partition strategies (`partitionMain`)

`PartitionStrat=auto` (the default) resolves to one of the three strategies below based on detected firmware and `Encryption` — see [synos.conf Reference](./synos-conf.md#partitionstratauto-resolution) for the exact resolution logic and why it exists.

### `uefi-bootctl`

GPT label, two partitions, created by `partitionStrat_uefi_bootctl`:

1. ESP — FAT32, sized `BootSize` (default `512MiB`), `esp on` flag set, mounted separately at `BootMountLocation`.
2. Root — everything else.

Partition device nodes are resolved with a `${Disk}p1`/`${Disk}1` fallback so this works on both `/dev/sdX`-style and `/dev/nvme0n1`-style naming. Supports `Encryption=yes` or `no` — `systemd-boot` (installed via `bootctl`) needs nothing special either way, since decryption happens in the initramfs, not the bootloader.

### `mbr-syslinux`

MSDOS label, one partition spanning the whole disk (`partitionStrat_mbr_syslinux`). `BootPart` is set equal to `RootPart` — syslinux lives inside the root filesystem itself, and there is no separate boot partition in this strategy.

**`Encryption` must be `no`.** syslinux has no LUKS support at all — it cannot read from an encrypted partition under any circumstances — and because this strategy has no separate unencrypted boot partition to fall back to, there is nowhere for it to read `syslinux.cfg`/the kernel/initramfs from if root is encrypted. `syn-config.zsh` enforces this at config-load time: `PartitionStrat=mbr-syslinux` combined with `Encryption=yes` is a hard config error, before Stage 0 ever touches the disk.

### `mbr-grub`

MSDOS label, two partitions (`partitionStrat_mbr_grub`) — the same shape as `uefi-bootctl`, just for legacy BIOS:

1. Boot — ext4, sized `BootSize`, mounted separately at `BootMountLocation`.
2. Root — everything else, encrypted or not.

This strategy exists specifically to close the gap `mbr-syslinux` can't: BIOS/MBR installs with `Encryption=yes`. GRUB needs to read `grub.cfg` and the kernel/initramfs from *somewhere* before it can decrypt anything, so unlike `mbr-syslinux`, `mbr-grub` always creates its own small unencrypted `/boot` — even when `Encryption=no`, for a consistent partition layout regardless of the encryption choice. GRUB itself never touches the encrypted root directly: `/boot` holds the plain kernel and initramfs, GRUB boots those with `cryptdevice=UUID=...` baked into the kernel command line (see [Stage 1](./stage1.md)), and the initramfs's `encrypt` hook performs the actual unlock at boot. This is the same division of labour `uefi-bootctl` already uses — GRUB's own native LUKS support (`cryptomount`) is deliberately not used here, since `grub-install` in Stage 1 only loads `part_msdos`, `ext2`, and `biosdisk` modules, nothing crypto-related.

All three strategies zero the first 4 MiB of the disk (`partitionMain`, before dispatching) to clear stale partition-table signatures that can otherwise confuse `parted`/the kernel on a reused disk, and `wipefs -a` every partition they create afterward to clear filesystem/LUKS/LVM signatures that could persist deeper into a reused partition than the 4 MiB zero reaches.

## Encryption and LVM: `Encryption` / `UseLvm`

Two independent yes/no flags in `synos.conf`, not one compound strategy string:

```
Encryption="yes"   # yes | no: whole-disk LUKS2 on root
UseLvm="yes"       # yes | no: LVM on top of root (or on top of the LUKS mapper, if both are yes)
```

`syn-config.zsh` combines these into an internal `VolumeStrat` value that `volumeMain` (in `syn-disk.zsh`) dispatches on — this is what ultimately determines `RootFsDev`, the device `filesystemMain` actually runs `mkfs` against:

| `Encryption` | `UseLvm` | `VolumeStrat` | Function | Layers | `RootFsDev` |
|---|---|---|---|---|---|
| yes | yes | `luks-lvm` | `volumeStrat_luks_lvm` | LUKS2 → LVM (PV/VG) → LV | `/dev/${VgName}/${LvRootName}` |
| yes | no | `luks-only` | `volumeStrat_luks_only` | LUKS2 | `/dev/mapper/${LuksLabel}` |
| no | yes | `lvm-only` | `volumeStrat_lvm_only` | LVM (PV/VG) → LV | `/dev/${VgName}/${LvRootName}` |
| no | no | `plain` | `volumeStrat_plain` | none | `${RootPart}` directly |

**LUKS** (`Encryption=yes`, either `luks-lvm` or `luks-only`): `cryptsetup luksFormat --type luks2 --cipher "${LuksCipher}" --key-size "${LuksKeySize}" --pbkdf "${LuksPbkdf}" --batch-mode --key-file=- "${RootPart}"`, passphrase piped in from `LuksPassphrase`. Opened immediately after with `cryptsetup open` under the mapper name `LuksLabel`, giving `RootMapper=/dev/mapper/${LuksLabel}`. The volume's UUID is captured via `cryptsetup luksUUID "${RootPart}"` into `LuksUuid` and exported — Stage 1 needs it for both the `encrypt` mkinitcpio hook and the `cryptdevice=UUID=...` kernel parameter (see [Stage 1](./stage1.md)).

**LVM** (`UseLvm=yes`, either `luks-lvm` or `lvm-only`): a single volume group named `VgName` is created (`pvcreate -ffy` + `vgcreate`) on top of whatever device precedes it — the LUKS mapper in `luks-lvm`, the raw partition in `lvm-only`. If `SwapSize` is not `"0"`, a swap logical volume (`LvSwapName`) is carved out first with `lvcreate -L "${SwapSize}"`; the root logical volume (`LvRootName`) then takes `lvcreate -l 100%FREE`, i.e. all space left in the VG — there's no separate field to size root explicitly.

**Swap** only exists under `luks-lvm` or `lvm-only`, since only those two strategies have an LVM layer to carve a swap LV from — `plain` and `luks-only` have no such layer, so `SwapDev` stays empty regardless of `SwapSize`. Swap is off by default (`SwapSize="0"` in the shipped `synos.conf`).

**`plain`** (`Encryption=no`, `UseLvm=no`): `volumeStrat_plain` does no work beyond pointing `RootMapper`/`RootFsDev` at `RootPart` directly — `partitionMain` already `wipefs`'d the partition, so there's nothing left to prepare.

## Filesystem strategies (`filesystemMain`)

A thin dispatcher over four `mkfs` variants, all labeled `ROOT`:

| `FilesystemStrat` | Command |
|---|---|
| `ext4` | `mkfs.ext4 -F -L ROOT` |
| `f2fs` | `mkfs.f2fs -f -l ROOT` |
| `btrfs` | `mkfs.btrfs -f -L ROOT` |
| `xfs` | `mkfs.xfs -f -L ROOT` |

Before formatting, `filesystemMain` runs `modprobe "${FilesystemStrat}"` (best-effort) — `mkfs.*` only needs the userspace tool to be installed, but `mount`'s filesystem auto-detection later needs the kernel module already loaded, and it isn't guaranteed to autoload on the live image. If `SwapDev` was set by the volume stage, it's formatted too (`mkswap -L SWAP`).

The shipped default is `f2fs`, a flash-friendly log-structured filesystem — a reasonable default for SSD/NVMe-only installs, worth changing to `ext4` for spinning disks or wider tooling compatibility. SYN-OS doesn't script any btrfs-specific features (subvolumes, snapshots) — choosing `btrfs` here just gets the plain filesystem with no extra behavior layered on top. Adding a fifth filesystem type means adding both a case here and its `mkfs` package to `baseCore` in [Package Collection](./packages.md).

## Mounting (`mountMain`)

Mounts `RootFsDev` at `RootMountLocation` (default `/mnt`). If there's a separate boot partition (`BootPart` differs from `RootPart` — true for `uefi-bootctl` and `mbr-grub`, false for `mbr-syslinux`), mounts it at `BootMountLocation` (default `/mnt/boot`, and `syn-config.zsh` enforces that it's a subpath of `RootMountLocation`). Activates swap with `swapon` if `SwapDev` is set.

## How this reaches Stage 1

None of the volume-layer facts — `RootFsDev`, `LuksUuid`, `RootMapper`, `SwapDev` — live in `synos.conf`. They're runtime values that only exist once Stage 0 has actually partitioned and formatted the disk, and they're written to `/etc/syn-os/install.state` at the end of `pacstrapMain` for Stage 1 to read after the chroot handoff. `Encryption`/`UseLvm` themselves, by contrast, do live in the `synos.conf` copy Stage 1 re-reads directly. See [Stage 0](./stage0.md) and [Stage 1](./stage1.md) for exactly where each value gets consumed.

## Setting these fields

All of the fields referenced above — `PartitionStrat`, `Encryption`, `UseLvm`, `FilesystemStrat`, `BootSize`, the `Luks*`/LVM fields — are set in `/etc/syn-os/synos.conf`. See [synos.conf Reference](./synos-conf.md) for the complete field list, defaults, and validation rules.
