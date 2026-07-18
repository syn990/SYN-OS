# How the Installer Works

SYN-OS installs in two stages, split by a chroot boundary. Stage 0 (`syn-stage0.zsh`) runs in the live ISO environment and turns a blank disk into a mounted, package-installed root filesystem. Stage 1 (`syn-stage1.zsh`) runs inside that freshly-pacstrapped system via `arch-chroot` and turns it into a bootable, configured install. You never run Stage 1 yourself — Stage 0 invokes it automatically as its last step. Both scripts live under `SYN-OS/SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/`.

![synos-install, start to finish: boot, edit synos.conf, Stage 0's pipeline, the chroot handoff, Stage 1's steps, installed system](./diagrams/svg/installer-overview.svg)

## Booting the ISO

The live ISO boots straight to a root zsh shell — no display manager, no live desktop. `/usr/lib/syn-os/` and `/etc/syn-os/synos.conf` are already on the image at that point.

## Starting the installer

The only input the installer takes is `/etc/syn-os/synos.conf`, edited by hand before anything runs — see [synos.conf Reference](./synos-conf.md) for every field. At minimum, `Disk` and `UserAccountPassword` (and `LuksPassphrase`, if `Encryption=yes`) must be changed from their shipped `CHANGE_ME` placeholders; both Stage 0 and Stage 1 hard-fail rather than proceed with the placeholder still in place. Once the config is ready:

```zsh
zsh /usr/lib/syn-os/syn-stage0.zsh
```

`syn-stage0.zsh` re-execs itself under `script -qefc` on its first invocation, so the entire run — Stage 0 and, once chrooted, Stage 1 — is captured to one timestamped transcript under `/root/`, without breaking `pacman`'s progress bars (see [Stage 0](./stage0.md#full-install-log) for why a plain pipe can't do this). It then sources `syn-config.zsh` (parses and validates `synos.conf`, resolves `PartitionStrat=auto` against the machine's real detected firmware), `syn-packages.zsh`, and `syn-ui.zsh`, asks for wipe confirmation unless `RequireWipeConfirm=no`, and runs the disk-prep and pacstrap pipeline.

## Stage 0: orchestrator

`syn-stage0.zsh` itself is a thin sequence — the real logic lives in `syn-disk.zsh` and `syn-pacstrap.zsh`, both of which it sources before calling, in fixed order:

```
partitionMain -> volumeMain -> filesystemMain -> mountMain -> pacstrapMain
```

`partitionMain`, `volumeMain`, `filesystemMain`, and `mountMain` all live in `syn-disk.zsh` and dispatch on the strategy selectors `syn-config.zsh` resolved from `synos.conf` (`PartitionStrat`, `VolumeStrat`, `FilesystemStrat`) — see [Storage Strategies](./storage-strategies.md) for the full matrix. `pacstrapMain` lives in `syn-pacstrap.zsh`: it refreshes mirrors and the pacman keyring, installs the package set chosen by `PackageProfile` (see [Package Collection](./packages.md)), deploys the dotfile overlay and this documentation onto the target, and writes `/etc/syn-os/install.state` — the handoff file Stage 1 reads for facts Stage 0 only knows once disk prep has actually happened (real partition device paths, the LUKS UUID `cryptsetup` just generated). Full detail: [Stage 0](./stage0.md).

## Stage 1: in-chroot configuration

Once `pacstrapMain` returns, `syn-stage0.zsh` runs:

```zsh
arch-chroot "$RootMountLocation" /bin/zsh /usr/lib/syn-os/syn-stage1.zsh
```

Stage 1 re-sources `syn-config.zsh` (reading the `synos.conf` copy `pacstrapMain` placed on the target) and sources `install.state` for the disk facts above — if that file is missing it exits immediately rather than guessing. In order, it then: sets locale/hostname/timezone/console, sets up the `doas`→`sudo` shim, creates the user account and sets its password, builds `syn-filemanager` from source as that new user (`cmake` + `qt6-base`, both already pacstrap'd), configures and rebuilds the initramfs (`configure_mkinitcpio` adds the `encrypt` hook when `Encryption=yes` and `lvm2` when `UseLvm=yes`), installs the bootloader matching `PartitionStrat` (`bootctl`, `syslinux-install_update`, or `grub-install`), enables `dhcpcd`/`iwd` plus the conditional `sshd` and `qemu-guest-agent`, and prints a final banner. Full detail: [Stage 1](./stage1.md).

Control returns to `syn-stage0.zsh` once the chroot exits; it copies the full install log onto the newly installed disk so it survives past the live session, and the install is done.

## What's not built yet

There is no graphical installer, no TUI, and no partition-picker — `synos.conf` is the entire interface, edited in a text editor before the first command runs. There's no dual-boot detection or resize-existing-partition support: `partitionMain` always works against a disk it's about to wipe (see [Storage Strategies](./storage-strategies.md)), never an existing layout. There's no post-install first-run wizard — whatever `synos.conf` said at install time (including the theme selected via [Dotfile Overlay](./dotfile-overlay.md)) is what you get on first boot.
