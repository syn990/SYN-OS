# Storage Strategies

SYN-OS separates "how is the disk laid out" (`PartitionStrat`), "is root encrypted / on LVM" (`Encryption`/`UseLvm`), and "what filesystem is on root" (`FilesystemStrat`) into independent config keys in [`synos.conf`](./synos-conf.md), each handled by its own dispatcher function in `syn-disk.zsh`. This modularity means swapping encryption on/off or changing filesystem doesn't touch partitioning code.

## Partition strategies: `syn-disk.zsh` (`partitionMain`)

`PartitionStrat=auto` (the default) picks one of the three below based on detected firmware and `Encryption`. See [synos.conf](./synos-conf.md#partitionstratauto-how-it-resolves-and-why-it-matters) for the resolution table and why this replaced a static default that could silently mismatch real hardware.

### `uefi-bootctl`
GPT label, two partitions:
1. ESP (FAT32, sized `BootSize`), mounted separately at `BootMountLocation`.
2. Root: everything else.

Boot device nodes are resolved with a `pN`-then-`N` fallback so it works on both `/dev/sdX` and `/dev/nvme0n1` naming. Supports `Encryption=yes` or `no`.

### `mbr-syslinux`
MSDOS label, one partition spanning the whole disk. `BootPart` and `RootPart` point at the same device: syslinux lives inside the root filesystem, and there's no separate boot partition.

**`Encryption` must be `no`** for this strategy. syslinux has zero LUKS support (it cannot read from an encrypted partition under any circumstances), and with no separate boot partition there's nowhere unencrypted for it to fall back to. `syn-config.zsh` rejects `mbr-syslinux` + `Encryption=yes` at config-load time with an error pointing at `mbr-grub`.

### `mbr-grub`
MSDOS label, two partitions, same shape as `uefi-bootctl`, just for legacy BIOS:
1. Boot (ext4, sized `BootSize`), mounted separately at `BootMountLocation`.
2. Root: everything else, encrypted or not.

This is the BIOS/MBR strategy to use when you want `Encryption=yes` on legacy hardware/VMs. GRUB itself never touches the encrypted root directly: `/boot` holds the plain (unencrypted) kernel and initramfs, GRUB just boots those with `cryptdevice=UUID=...` on the kernel command line, and the initramfs's `encrypt` hook does the actual unlock at boot time. This is the same division of labour `uefi-bootctl` already uses; GRUB's own LUKS support (`cryptomount`) is deliberately not used here, since it's historically been fragile for LUKS2 with modern KDFs like `argon2id` on legacy BIOS.

All three strategies zero the first 4 MiB of the disk before partitioning, to clear stale partition table signatures that can otherwise confuse `parted`/the kernel on reused disks.

## Encryption and LVM: `Encryption` / `UseLvm`

Two independent yes/no flags in `synos.conf`, rather than one compound strategy string:

```
Encryption="yes"   # yes | no: whole-disk LUKS2 on root
UseLvm="yes"        # yes | no: LVM on top of root (or on top of the LUKS mapper, if both are yes)
```

`syn-config.zsh` combines these into an internal `VolumeStrat` value that `syn-disk.zsh`'s `volumeMain` dispatches on: this is what determines what `RootFsDev` (the device `mkfs` actually targets) ends up being:

| `Encryption` | `UseLvm` | Internal `VolumeStrat` | Layers | `RootFsDev` becomes |
|---|---|---|---|---|
| yes | yes | `luks-lvm` | LUKS2 → LVM (PV/VG) → LV | `/dev/<VgName>/<LvRootName>` |
| yes | no | `luks-only` | LUKS2 | `/dev/mapper/<LuksLabel>` |
| no | yes | `lvm-only` | LVM (PV/VG) → LV | `/dev/<VgName>/<LvRootName>` |
| no | no | `plain` | none | the raw partition (`RootPart`) |

**LUKS** (`Encryption=yes`): `cryptsetup luksFormat --type luks2` with `LuksCipher`/`LuksKeySize`/`LuksPbkdf` from config, opened under `LuksLabel`. The resulting UUID (`LuksUuid`) is captured and threaded through to Stage 1, which needs it for both the mkinitcpio `encrypt` hook and the `cryptdevice=` kernel parameter.

**LVM** (`UseLvm=yes`): a single VG (`VgName`) is created on top of whatever device precedes it (LUKS mapper or raw partition). If `SwapSize` is non-zero, a swap LV is carved out first; the root LV then takes `100%FREE`, meaning all remaining space, so there's no separate "how big should root be" field to configure.

**Swap** is optional and off by default (`SwapSize="0"` in the shipped `synos.conf`). It only exists when `UseLvm=yes`: `plain` and `Encryption=yes`+`UseLvm=no` have no LV layer to carve swap from, so swap isn't available under those.

## Filesystem strategies: `syn-disk.zsh` (`filesystemMain`)

A thin dispatcher over four `mkfs` variants, all labeled `ROOT`:

| `FilesystemStrat` | Command |
|---|---|
| `ext4` | `mkfs.ext4 -F -L ROOT` |
| `f2fs` | `mkfs.f2fs -f -l ROOT` |
| `btrfs` | `mkfs.btrfs -f -L ROOT` |
| `xfs` | `mkfs.xfs -f -L ROOT` |

The shipped default is `f2fs`, a flash-friendly log-structured filesystem. It's a reasonable default for SSD/NVMe-only installs but worth changing to `ext4` or `btrfs` if you're targeting spinning disks or want snapshot support (SYN-OS doesn't currently script any btrfs-specific features like subvolumes or snapshots, so choosing `btrfs` here just gets you the plain filesystem).

## How this reaches Stage 1

None of the volume-layer decisions (LUKS UUID, mapper paths, VG name) live in `synos.conf` once resolved. They're runtime facts that only exist after Stage 0 has actually partitioned and formatted the disk. They get written to `/etc/syn-os/install.state` (along with `Encryption`/`UseLvm` themselves) at the end of Stage 0 and read back by Stage 1, which uses them to build the correct `mkinitcpio` hooks and kernel command line. See [Stage 1](./stage1.md).
