# Stage 1: In-Chroot Configuration

Stage 1 (`syn-stage1.zsh`) runs automatically as the last step of Stage 0, via `arch-chroot`. You do not run it yourself; the live-environment `.zshrc` warns against this explicitly, because Stage 1 depends on state that only exists once you're inside the chroot.

## Where its input comes from

Stage 1 sources `syn-config.zsh` again (for `synos.conf` values like `Hostname`, `Locale`), but disk-specific values, `RootFsDev`, `LuksUuid`, `SwapDev`, `VgName`, aren't in `synos.conf` at all; they're *resolved* during Stage 0's partition/volume steps. Those get read from `/etc/syn-os/install.state`, written by `pacstrapMain` in [Stage 0](./stage0.md) just before the chroot handoff. If that file is missing, Stage 1 exits immediately rather than guessing.

## What it does, in order

1. **Locale / hostname / time / console**: writes `/etc/locale.gen`, runs `locale-gen`, writes `/etc/locale.conf` and `/etc/hostname`, symlinks `/etc/localtime` (falls back to `Europe/London` if the configured `TimeZone` doesn't resolve to a valid zoneinfo file), writes `/etc/vconsole.conf`, syncs the hardware clock.

2. **doas/sudo shim**: if `doas` is installed (it's in `baseCore`), writes `/etc/doas.conf` granting the `wheel` group persistent permit, then replaces `/usr/bin/sudo` with a one-line shim that execs `doas`, and removes the actual `sudo` package. This means `sudo` "works" on the installed system but is doas underneath.

3. **User account**: creates `UserAccountName` with `UserShell`, adds it to `wheel`, and prompts interactively for a password via `passwd </dev/tty` (redirected from the real terminal since stdin inside `arch-chroot` from a script isn't the tty by default).

4. **mkinitcpio hooks**: rewrites the `HOOKS=` line in `/etc/mkinitcpio.conf`. The base hook set is always `base udev autodetect modconf kms keyboard keymap consolefont block`, with `encrypt` inserted if `Encryption=yes`, `lvm2` inserted if `UseLvm=yes`, then `filesystems fsck` appended. Runs `mkinitcpio -P` to rebuild the initramfs with the new hook set.

5. **Bootloader**: constructs the kernel command line first:
   - `Encryption=yes` gets `cryptdevice=UUID=<LuksUuid>:<LuksLabel> root=<RootFsDev> rw`.
   - `Encryption=no` resolves the root device's UUID via `blkid` and uses `root=UUID=... rw` (falling back to the raw device path if `blkid` can't resolve it).
   - If a swap device exists, appends `resume=UUID=<swap-uuid>` for hibernation support.

   Then, based on `PartitionStrat`:
   - **`uefi-bootctl`**: runs `bootctl --path=/boot install`, writes `loader.conf` (`timeout 0`, no editor), and writes a `syn.conf` boot entry pointing at `/vmlinuz-linux` with the appropriate initrd lines (conditionally including `intel-ucode.img`/`amd-ucode.img` if present).
   - **`mbr-syslinux`**: runs `syslinux-install_update -i -a -m`, then patches the `APPEND` line in `syslinux.cfg` with the constructed command line. (Never reached with `Encryption=yes`: `syn-config.zsh` rejects that combination before Stage 0 even starts.)
   - **`mbr-grub`**: runs `grub-install --target=i386-pc --recheck --boot-directory=/boot` targeting the whole disk (not a partition, since BIOS GRUB needs the MBR gap), then hand-writes `/boot/grub/grub.cfg` with a single `menuentry` using the same constructed command line. GRUB itself never decrypts anything here: `/boot` is a plain, unencrypted ext4 partition (see [Storage Strategies](./storage-strategies.md#mbr-grub)), so `grub-install` only needs the standard BIOS + `ext2` modules, no `cryptomount`/`luks2`. If `/boot/grub/splash.png` exists it's used as a background image; nothing ships one yet, so this degrades to a plain text menu.

6. **Services**: enables `dhcpcd` and `iwd` (both best-effort; failures are swallowed since not every install needs both).

7. **Final banner**: `syn_ui::final_banner` prints completion, and control returns to Stage 0, which had been waiting on the `arch-chroot` call.

## After Stage 1 finishes

Stage 0 returns from the `arch-chroot` call and the installer script ends. Reboot manually, remove the USB, and log in as `UserAccountName`. From there, running `synos` launches the desktop; see [First Boot](../readme.md#first-boot) and [Waybar](./waybar.md) / [LabWC](./labwc.md).
