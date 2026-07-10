# How the Installer Works

SYN-OS installs in two stages, split by a chroot boundary. Stage 0 runs in the live ISO environment and prepares the disk; Stage 1 runs inside the freshly-installed system (via `arch-chroot`) and configures it. You never run Stage 1 yourself: Stage 0 invokes it automatically as its last step.

## Booting the ISO

The ISO supports both boot modes (`profiledef.sh`'s `bootmodes=(bios.syslinux uefi.systemd-boot)`), each with its own menu:

- **BIOS/MBR**: `syslinux` (`SYN-ISO-PROFILE/syslinux/syslinux.cfg`) shows a full branded menu: boot the live environment (normal or to-RAM), network install over NBD/NFS/HTTP, boot an existing OS, Memtest86+, hardware info (HDT), reboot, and power off. Defaults to booting the live environment after 15 seconds.
- **UEFI**: `systemd-boot` (`SYN-ISO-PROFILE/efiboot/loader/`) is deliberately minimal: systemd-boot has no background-image support, so there's no equivalent of syslinux's splash screen. One entry (`SYN-OS Live`), reboot/poweroff available via systemd-boot's own auto-detected entries (enabled in `loader.conf`), 15-second timeout.

Both boot straight into the same live environment; from there the install steps are identical.

## Starting the installer

Once booted, you land in a zsh shell (`airootfs/etc/zsh/zshrc`). It prints a warning, tells you where the config lives, and defines one alias:

```zsh
alias synos-install="/usr/lib/syn-os/syn-stage0.zsh"
```

Before running it, edit `/etc/syn-os/synos.conf`: see [synos.conf, Declarative Strategy Selection](./synos-conf.md). This file is the only input to the installer; there are no interactive prompts for disk layout, filesystem, or encryption once `synos-install` starts.

## Stage 0: orchestrator

`syn-stage0.zsh` (`SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/syn-stage0.zsh`) does the following, in order:

1. Sources `syn-config.zsh` (loads and validates `synos.conf`), `syn-packages.zsh` (package arrays), and `ui.zsh` (splash/banner helpers).
2. Sources the strategy modules: `syn-partition.zsh`, `syn-volume.zsh`, `syn-filesystem.zsh`, `syn-mount.zsh`, `syn-pacstrap.zsh`.
3. **Safety gate:** if `RequireWipeConfirm=yes` (the default), it interactively asks "Proceed and wipe `<disk>`? [y/N]"; anything other than an explicit `y`/`yes` aborts. This is the only interactive confirmation in the entire pipeline, and everything after this point runs unattended and is destructive.
4. Runs the pipeline functions in sequence: `partitionMain` → `volumeMain` → `filesystemMain` → `mountMain` → `pacstrapMain`.
5. Prints a summary of what was mounted where.
6. Runs `arch-chroot "$RootMountLocation" /bin/zsh /usr/lib/syn-os/syn-stage1.zsh`, the handoff into Stage 1.

See [Stage 0, Pre-Chroot Setup](./stage0.md) for what each pipeline function actually does, and [Storage Strategies](./storage-strategies.md) for the partition/volume/filesystem options.

## Stage 1: in-chroot configuration

Stage 1 doesn't re-read `synos.conf` directly for disk state. `pacstrapMain` (the last step of Stage 0) writes a state file to `/etc/syn-os/install.state` inside the target system, capturing resolved values like `RootFsDev`, `LuksUuid`, and `SwapDev` that only exist after partitioning/formatting actually happened. Stage 1 sources that file, then:

1. Sets locale, hostname, timezone, console keymap/font.
2. Installs a `doas`→`sudo` shim if `doas` is present, and removes the `sudo` package.
3. Creates the user account from `UserAccountName`/`UserShell` and prompts for a password.
4. Regenerates `mkinitcpio.conf` hooks based on the volume strategy (adds `encrypt` for LUKS, `lvm2` for LVM) and rebuilds the initramfs.
5. Installs the bootloader (`systemd-boot` via `bootctl`, `syslinux`, or `grub`) with the correct kernel command line for the encryption/LVM setup chosen.
6. Enables `dhcpcd` and `iwd`.

See [Stage 1, In-Chroot Configuration](./stage1.md) for details. Once Stage 1 finishes, the installed system is just Arch plus whatever `SYNSTALL` installed: `pacman -Syu` is the ongoing upgrade path from here, same as any Arch system.

## What's not built yet

There is no unattended/non-interactive installer mode, no TUI, and no partition-picker. `synos.conf` is edited by hand in a text editor before running `synos-install`. That's a deliberate design choice (see the [Philosophy](../readme.md#philosophy) section of the README), not a missing feature.
