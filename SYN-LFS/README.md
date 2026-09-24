# SYN-OS

SYN-OS built from source. The base is Linux From Scratch 13.1 from the multilib book, built by jhalfs: the LFS packages, plus 32-bit copies of the core libraries next to the 64-bit ones. runit is PID 1 and zsh is part of the base. `desktop/` builds the SYN desktop on that base from 175 recipes, and `iso/` turns the result into an installer ISO.

Everything happens in a 20G disk image under `~/SYN-OS-build` (`SYN_OS_WORK` moves it). Nothing on the build machine's own disks or bootloader is touched. The kernel is one EFI-stub file with its command line built in, so an installed system has no bootloader and no initramfs. It finds its root by partition label (`root=PARTLABEL=synroot`), so the same image boots in QEMU or written to a USB stick.

## Files

- `build.zsh` — the build, one step per invocation (below).
- `pipeline.zsh` — every step in order, tree to ISO, in one terminal; `pipeline.zsh STEP` carries on from a step.
- `kernel.syn`, `kernel-sound.syn` — kernel options merged onto `defconfig`: the SYN options, and the sound drivers.
- `fstab` — the installed system's fstab, by partition label.
- `bin/sudo` — a `sudo` front end to doas, for jhalfs.
- `runit/` — `install.zsh` puts runit in as PID 1 (sysvinit stays; `init=/usr/sbin/init.sysv` boots the SysV way). `1`, `2`, `3` and `ctrlaltdel` are the stages. `sv/` holds the base services: gettys on tty1, tty2 and ttyS0, udevd, syslogd.
- `desktop/` — `order` (the recipe sequence, its categories and profiles), `syn-pkg.zsh` (`fetch`, `build`, `list`), `lib.zsh` (helpers for recipes), `pkgs/` (one recipe per package), `files/` (config and service files the recipes install).
- `iso/` — `init` (the live initramfs's init), `mkinitramfs.zsh`, `sanitize-accounts.zsh`, `grub.cfg`, `synos-install.zsh` (`synos-install`), `synos.conf`, and `live/` (the live session's files).

## Building it

`./pipeline.zsh` runs the whole thing below, image to ISO, and stops at the first step that fails; `./pipeline.zsh STEP` picks up from that step. The steps one at a time:

The build host needs zsh, doas, git, python3, curl, gptfdisk, dosfstools, e2fsprogs, and QEMU with OVMF in `/usr/share/edk2/x64` to boot the result. jhalfs is checked out in `~/SYN-OS-build/jhalfs`.

```
./build.zsh image        # the disk image, partitioned and mounted
./build.zsh kernel       # kernel .config: defconfig + kernel.syn + kernel-sound.syn
./build.zsh configure    # jhalfs's configuration
./build.zsh build        # the base, a few hours
./build.zsh zsh          # zsh into the base
./build.zsh runit        # runit as PID 1
./build.zsh fetch        # the desktop's sources, no root needed (about 1.5G for minimal)
./build.zsh desktop      # the desktop, hours (LLVM, Mesa, Qt and FFmpeg are most of it)
./build.zsh kernel       # again: the CPU microcode is in the image now, to build in
./build.zsh rekernel
```

`zsh` comes before `runit`: the stage scripts are zsh, and `runit` refuses without it.

`SYN_OS_PROFILE=full|minimal` picks the profile for `fetch` and `desktop` (default `full`). `desktop` picks up where it stopped: a package that fails leaves its build tree in `/tmp/syn-build` and its log in `/var/log/syn-os` inside the image, and running `desktop` again starts from that package. `desktop NAME...` rebuilds just those.

Then either make the installer ISO:

```
./build.zsh iso          # the ISO, in ~/SYN-OS-build
./build.zsh isotest      # boot it in QEMU with a blank 40G disk and install onto that
./build.zsh isotest disk # then boot the installed disk
```

or use the build image as a test system:

```
./build.zsh user NAME    # a login user (wheel, video, audio, input; zsh) and its password
./build.zsh esp          # the kernel onto the EFI partition as BOOTX64.EFI
./build.zsh umount
./build.zsh gui          # QEMU with a virtio GPU, input and sound; `boot` is the serial console only
```

Log in and type `synos`. `SYN_OS_DISPLAY` picks QEMU's display (`gtk,gl=on` by default; `sdl` or `gtk` without GL draws with llvmpipe), `SYN_OS_QEMU` adds QEMU arguments, such as a USB Wi-Fi stick.

If `kernel.syn` changes after the base is built, `mount`, `kernel`, `rekernel` and `esp` rebuild just the kernel. An image built before the base went multilib has no 32-bit libraries: move `synos.img` away and start from `image`. Fetched sources are reused.

## Profiles

`desktop/order` lists the recipes in build order, each after everything it needs. A `# [category]` header puts the recipes under it in a category, and two lines at the top declare the profiles as sets of categories:

```
profile full    baseCore netAndServices shellAndCLI desktopStack fontsI18n appsMedia synOS
profile minimal baseCore netAndServices shellAndCLI desktopStack fontsI18n synOS
```

A profile filters the sequence and never reorders it. `minimal` (158 recipes) is a desktop that boots: everything but `appsMedia`. `full` (176) adds `appsMedia`: Xwayland, Google surf with the libraries its binary links (NSS, NSPR, CUPS), and Steam with the 32-bit libraries it needs. Running `desktop` with `SYN_OS_PROFILE=full` after a `minimal` build builds only those. `zsh desktop/syn-pkg.zsh list` prints a profile's recipes.

## Recipes

A recipe in `desktop/pkgs/NAME` sets `v` (the version), `src` (one `"url checksum [filename]"` per file; the first is unpacked, the rest sit next to it) and `build()`, which runs as root inside the unpacked source with `lib.zsh` loaded and `err_exit`, `pipe_fail` and `sh_word_split` on. Checksums are `md5:` (the BLFS book's), `sha256:`, or `-` for the two files with no fixed release (surf, the Mozilla CA list). `noextract=1` leaves the file packed; `always=1` rebuilds every run (the SYN tools, built from this repo). Where BLFS r13.1 has the package, the version, checksum and commands are the book's. The rest (labwc, waybar, foot, fuzzel, iwd, rofi, Steam and the others) are pinned to a release with a sha256.

`lib.zsh` gives recipes `meson_build` and `cmake_build`; `configure32`, `meson_build32` and `cmake_build32` for the 32-bit builds (a staging install, with `/usr/lib32` copied over); `svc NAME [off]`, a runit service with an svlogd log in `/var/log/sv/NAME`, enabled unless `off`; and `syn_tool NAME`, a SYN-SOFTWARE tool built with CMake.

Sources are downloaded on the host into `~/SYN-OS-build/sources/desktop` and the chroot builds offline: it sees them at `/sources/syn-desktop`, and this repo read-only at `/usr/src/SYN-OS`. Built versions are recorded in `/var/lib/syn-os/pkgs`; logs go to `/var/log/syn-os`.

## The system

runit's three stages and every service's `run` script are zsh (`#!/bin/zsh -f`).

- Stage 1 (`runit/1`) mounts the kernel filesystems, loads `/etc/sysconfig/modules`, coldplugs udev, checks and mounts everything in fstab, turns swap on, clears `/tmp`, makes `/run/user/UID` for every user with a UID from 1000 to 59999, brings up `lo` and the hostname, sets the clock from the hardware clock (UTC), loads the console keymap and font from `/etc/sysconfig/console`, applies `/etc/sysctl.conf` and brings up any `ifconfig.*` marked `ONBOOT=yes`. A failed fsck or remount drops to `sulogin`.
- Stage 2 raises the open-files hard limit for everything it starts (Proton's esync needs it; there is no PAM limits file) and runs `runsvdir` over `/var/service`.
- Stage 3 stops the services, kills what is left, saves the clock and unmounts.
- Services: gettys on tty1, tty2 and ttyS0 (115200), udevd and syslogd from the base; dbus, seatd, iwd, dhcpcd and ntpd from the desktop, with sshd and bluetoothd installed but off. Enable one with `ln -s /etc/sv/NAME /var/service/`. Each logs through svlogd to `/var/log/sv/NAME`.

There is no systemd and no logind:

- seatd gives labwc its seat; users in the `video` group can start a session.
- iwd joins networks and dhcpcd gets the address.
- PipeWire, WirePlumber and pipewire-pulse start from labwc's autostart (`syn-pipewire-session.zsh`).
- mako and syn-connect talk D-Bus through basu, the standalone sd-bus library.
- Reboot and power off in the menus run runit's `reboot` and `poweroff` through doas.
- Steam's controller udev rules give devices to the `input` group; `user` puts you in it.

The dotfiles are `SYN-ISO-PROFILE`'s `DotfileOverlay`, installed by the `syn-desktop` recipe with these changes:

- labwc's menu is `desktop/files/labwc/menu.xml`: surf and Steam at the top, All Applications built from the installed .desktop files (`syn-pipe-apps.zsh`), config files opening in nano, and Preferences > Software > Services toggling runit services (`syn-services-toggle.zsh`). No SYN-SHARE, BlackArch or ISO-builder entries.
- waybar has no SYN-SHARE module. The stats and relay modules, network, backlight (brightnessctl, with its udev rule), audio and the power menu are there.
- syn-sysmon's Logs view reads the svlogd logs and syslogd's files in `/var/log`.
- labwc's environment sets the Adwaita cursor and no `QT_QPA_PLATFORMTHEME` (qt6ct is not built). GTK defaults to dark Adwaita, which SYN's `gtk.css` recolours.
- `xdg-open` sends links, PDFs and images to surf, text to nano and folders to syn-filemanager (`/etc/xdg/mimeapps.list`).
- The console keymap is `uk` and the font `ter-v16n` (`/etc/sysconfig/console`).

Also there: git, openssh, fzf, zoxide, ripgrep, fd, bat, btop, glow, tree, graphviz, the three zsh plugins, wf-recorder and waypipe. fzf, zoxide, ripgrep, fd, bat and glow are upstream's prebuilt release binaries (Go and Rust programs; LFS has neither toolchain). surf is Google's .deb unpacked into `/opt`, on Wayland. Steam is Valve's launcher; the `lib32-*` recipes give it and 32-bit games Mesa with a 32-bit LLVM, the X11 and Vulkan libraries, ALSA through PipeWire and NSS, from the same sources as the 64-bit builds. Steam downloads its own runtime on first run, into `~/.local/share/Steam`. Firmware: linux-firmware, SOF audio firmware with the full set of sound drivers (`kernel-sound.syn`), and Intel and AMD CPU microcode built into the kernel.

## The installer ISO

`iso` needs squashfs-tools, grub, libisoburn, mtools, libarchive and zstd on the build host, and runs after `desktop`, `kernel` and `rekernel` (the kernel needs the live-boot options and the microcode). It makes `~/SYN-OS-build/syn-os-DATE.iso`: the system as a squashfs (without `/sources`, the jhalfs tree, the build logs, `/home`, `/root` and any account added with `user`), the kernel, an initramfs holding `iso/init` and the programs it runs, and GRUB with the SYN-OS splash. Burn it or `dd` it onto a USB stick.

The initramfs finds the medium labelled `SYN_OS`, mounts the squashfs read-only under a tmpfs overlay, lays `/usr/share/syn-os/live/` over it and hands over to runit. `syn.toram` on the kernel command line copies the image into RAM first, so the stick can come out. The live session logs `synstigator` in on tty1 (password `synstigator`, doas without a password); `synos` starts the desktop from there. Nothing done in the live session reaches the installed disk.

`synos-install` takes its answers from the first of these it finds: a file named on its command line; a punchset; the ISO's own `/etc/syn-os/synos.conf` (`synos-config` opens it in nano). A punchset is a medium labelled `SYNPUNCH` with a filled-in `synos.conf` at its root (`mkfs.vfat -n SYNPUNCH /dev/sdX`, copy the file on): plug it in and the install reads it. Whatever the file leaves at `CHANGE_ME` (`Disk`, `UserAccountPassword`) is asked for on the terminal. The boot menu's "Install SYN-OS from a punchset" entry starts the install by itself once the live session is up; with `RequireWipeConfirm=no` in the punchset nothing is asked at all. It wipes `Disk`, makes a GPT with a FAT32 EFI system partition (`SYNEFI`) and an ext4 root (`synroot`), copies the clean squashfs across, puts the kernel at `EFI/BOOT/BOOTX64.EFI`, and sets the hostname, time zone, keyboard, locale and user from `synos.conf`. Nothing is downloaded. The ISO boots under BIOS too; installing needs UEFI.

## Not there yet

- Installing next to another OS, or onto an encrypted or LVM root: the installer takes the whole disk, and the kernel has no initramfs to unlock anything.
- SYN-SHARE, the BlackArch toggle and the ISO builder.
- GTK's icons: Adwaita's cursors are in; its icons are SVG and need librsvg, which needs Rust.
- qt6ct, so syn-filemanager does not follow the SYN theme. It needs qttools.
- The larger desktop apps: FeatherPad, surf, VLC, GIMP, Audacity, OBS.
- zenity, which Steam uses for some dialogs (GTK4 and libadwaita).
- Screen sharing from the browser (xdg-desktop-portal-wlr).
- A package manager: `/var/lib/syn-os/pkgs` records versions, not files.
