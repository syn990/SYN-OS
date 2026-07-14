# Filesystem Hierarchy

Linux organizes everything under a single root (`/`), with no drive letters, and standardized top-level directories whose meaning is mostly fixed by convention (the [Filesystem Hierarchy Standard](https://wiki.archlinux.org/title/Arch_filesystem_hierarchy_standard)). A few of these show up constantly across this repo's docs, so it's worth being precise about what each one means and where SYN-OS actually writes to them.

## `/etc`: system configuration

Machine-specific config, almost always plain text. SYN-OS writes its own config here at a dedicated path, `/etc/syn-os/`, rather than scattering files across the top-level `/etc`:

- `/etc/syn-os/synos.conf`: the installer's entire input; see [synos.conf](../synos-conf.md).
- `/etc/syn-os/install.state`: written by Stage 0, read by Stage 1; see [Stage 0](../stage0.md#5-pacstrapmain-syn-pacstrapzsh).

Standard Arch config also lives here: `/etc/fstab` (written by `genfstab`), `/etc/mkinitcpio.conf` (rewritten by [Stage 1](../stage1.md#5-mkinitcpio-hooks)), `/etc/locale.conf`, `/etc/hostname`.

## `/usr`: installed software and shared data

Where packages actually put their files: binaries typically under `/usr/bin`, shared libraries under `/usr/lib`, and static data under `/usr/share`. SYN-OS's own scripts live under `/usr/lib/syn-os/`, following the convention that `/usr/lib` holds a package's internal machinery, not just compiled libraries. This documentation, in turn, lives under `/usr/share/syn-os/docs/`, following the equally standard convention that `/usr/share` holds read-only, architecture-independent data. Both are copied there directly by [`pacstrapMain`](../stage0.md#5-pacstrapmain-syn-pacstrapzsh) at install time, not delivered as pacman packages.

`/usr/share/themes/` is the standard location desktop theme engines search. This is why SYN-OS's `SYN-OS-RED` theme lives there rather than somewhere custom; [LabWC](../labwc.md) and Qt theming tools both expect themes at this path by convention.

## `/home`: user data

Every user's personal files and dotfiles-in-place (`~/.config`, `~/.zshrc`, etc.). SYN-OS never templates directly into `/home/<user>`; see [Dotfile Overlay](../dotfile-overlay.md#why-etcskel-and-not-homeuser-directly) for why `/etc/skel` is the actual deployment target instead, with `useradd -m` doing the copy into each user's home at account-creation time.

## `/mnt`: temporary mount point

Conventionally where you mount something you're working with manually, rather than a location the system boots from, which is exactly SYN-OS's usage: `RootMountLocation` in [`synos.conf`](../synos-conf.md) defaults to `/mnt`, because Stage 0 runs from the live ISO and is mounting the *target* system's not-yet-active root there temporarily, before `arch-chroot` makes it "real" from Stage 1's perspective.

## Why this layout matters for SYN-OS specifically

Because [the installer](../installer-overview.md) mounts a real filesystem hierarchy at `/mnt` and then chroots into it, every one of these directories effectively exists twice during an install: once in the live environment (where scripts run from) and once under `/mnt` (the system being built). Assuming `/etc/syn-os/synos.conf` in the live environment and on the target system are the same file is a common source of confusion — they start as copies of each other ([`pacstrapMain`](../stage0.md#5-pacstrapmain-syn-pacstrapzsh) copies the live one onto the target explicitly) but diverge the moment either is edited independently afterward.
