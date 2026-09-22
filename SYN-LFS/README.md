# SYN-LFS

SYN-OS built from source instead of from Arch packages. The base is Linux From Scratch built by jhalfs, from the SysV multilib book: the same packages as LFS 13.1, plus 32-bit copies of the core libraries next to the 64-bit ones, which Steam needs. runit replaces sysvinit as PID 1. On top of that, `desktop/` builds the same labwc and waybar desktop the Arch ISO installs, plus Wi-Fi, sound, Xwayland, Google Chrome and Steam, and `iso/` turns the result into an installer ISO that looks and works like the SYN-OS one.

Everything happens in a 20G disk image under `~/SYN-LFS-build`. Nothing on the build machine's own disks or bootloader is touched. The kernel is one EFI-stub file with its command line built in, so there's no bootloader. It finds its root by partition label, so the same image boots in QEMU or written to a USB stick.

## Building it

On an Arch (or SYN-OS) machine with jhalfs checked out in `~/SYN-LFS-build/jhalfs`:

```
./build.zsh image        # the disk image, partitioned and mounted
./build.zsh kernel       # kernel .config: defconfig + kernel.syn + kernel-sound.syn
./build.zsh configure
./build.zsh build        # LFS itself, a few hours
./build.zsh runit
./build.zsh fetch        # the desktop's sources, about 1.6G (much of it firmware)
./build.zsh desktop      # the desktop, several hours (the two LLVMs are most of it)
./build.zsh kernel       # again: now the CPU microcode is in the image to build in
./build.zsh rekernel
```

Then either make the installer ISO:

```
./build.zsh iso          # the ISO, in ~/SYN-LFS-build
./build.zsh isotest      # boot it in QEMU with a blank disk and install onto that
./build.zsh isotest disk # then boot the installed disk
```

or use the build image itself as a test system:

```
./build.zsh user yourname
./build.zsh esp
./build.zsh umount
./build.zsh gui          # or `boot` for just the serial console
```

Log in and type `synos`, same as on Arch. Steam is in the launcher, or run `steam` from a terminal. The first run downloads the client into `~/.local/share/Steam`.

An image built before the base went multilib has no 32-bit libraries, so it has to be built again: move `lfs.img` out of the way and start from `image`. Sources already fetched are reused.

If `kernel.syn` changes after LFS is built, `mount`, `kernel`, `rekernel` and `esp` rebuild just the kernel.

## The installer ISO

`iso` needs `squashfs-tools`, `grub`, `libisoburn` and `mtools` on the build machine. It turns the image into a bootable ISO, which you can burn or `dd` onto a USB stick. It carries the system as built, minus `/sources`, the jhalfs tree, the build logs and anyone added with `user`.

It boots like the SYN-OS Arch ISO: the same GRUB splash and SYN-OS-RED menu, then the `synstigator` autologin and the SYN splash with its steps. Edit `/etc/syn-os/synos.conf` (`synos-config` opens it in nano) and at least set `Disk` and `UserAccountPassword`. Then `synos-install`. `synos` tries the desktop first, from the live session.

The installer copies the system from the ISO, so nothing is downloaded. It wipes `Disk`, makes a GPT with a FAT32 EFI system partition and an ext4 root, copies the system across, and puts the kernel where UEFI firmware looks for a boot file. It then sets up the hostname, time zone, keyboard, locale and your user from `synos.conf`. The live session runs on a RAM overlay, so nothing you do in it ends up on the installed disk. The ISO boots under BIOS too, but installing needs UEFI.

`desktop` picks up where it stopped. A package that fails leaves its build tree in `/tmp/syn-build` and its log in `/var/log/syn-lfs` inside the image, and running `desktop` again starts from that package. `desktop NAME` rebuilds just that one.

## How the desktop is put together

Each package is a small recipe in `desktop/pkgs` (version, source URL, checksum, build commands), built in the order listed in `desktop/order`. Where BLFS has the package, the version, md5 and commands come from the BLFS r13.1 book, so they match the LFS toolchain. The rest (labwc, waybar, foot, fuzzel, iwd, rofi, Steam and so on) are pinned to their current releases with sha256 sums. Sources are downloaded on the host and the chroot builds offline.

Where it differs from the Arch build, because there's no systemd and no logind:

- seatd gives labwc its seat, and users in the `video` group can start a session. runit stage 1 creates `/run/user/<uid>` at boot.
- D-Bus, seatd, iwd, dhcpcd and ntpd are runit services, and sshd and bluetoothd are installed but off, as on Arch. iwd joins networks and dhcpcd gets the address, same split as on Arch. Each service logs through svlogd to `/var/log/sv/NAME`.
- PipeWire, WirePlumber and pipewire-pulse are started from labwc's autostart (`syn-pipewire-session.sh`), since there are no user units to do it.
- mako and syn-connect talk D-Bus through basu, the standalone sd-bus library.
- Reboot and power off in the menus run runit's `reboot` and `poweroff` through doas.
- Steam's controller rules normally give devices to whoever is logged in through logind. Here they go to the `input` group instead, which `user` puts you in.
- There's no PAM limits file, so runit stage 2 raises the open-files hard limit for every login (Proton's esync wants it).

The dotfiles are the Arch ISO's DotfileOverlay, copied as they are, with these changes made by the `syn-desktop` recipe:

- labwc's menu is `desktop/files/labwc/menu.xml`, the Arch one with Chrome and Steam added at the top, All Applications built from the installed .desktop files (`syn-pipe-apps.zsh`, instead of archlinux-xdg-menu), config files opening in nano, and Preferences > Software > Services toggling runit services (`syn-services-toggle.zsh`). SYN-SHARE, BlackArch and the ISO builder are left out, since they're built on systemd units, pacman and archiso.
- waybar loses the SYN-SHARE module for the same reason. Everything else on the bar works: the stats and relay modules, network, backlight (brightnessctl, allowed by its udev rule), audio and the power menu.
- syn-sysmon's Logs view reads the svlogd logs and syslogd's files in `/var/log`, since there's no journal. The same source builds the journal version on Arch.
- labwc's environment sets the Adwaita cursor and drops qt6ct, which isn't built. GTK defaults to dark Adwaita, which SYN's `gtk.css` recolours.
- `xdg-open` sends links, PDFs and images to Chrome, text to nano and folders to syn-filemanager (`/etc/xdg/mimeapps.list`).
- The Docs viewer opens diagrams with `xdg-open` when there's no feh.

The command-line tools `.zshrc` and the scripts use are all there: git, openssh, fzf, zoxide, ripgrep, fd, bat, btop, glow, tree, graphviz, the three zsh plugins, wf-recorder for screen recording and waypipe for SYN-RELAY's remote apps. fzf, zoxide, ripgrep, fd, bat and glow are upstream's static release builds, as they're Go and Rust programs and LFS has neither toolchain.

Chrome is Google's own .deb unpacked into `/opt`, running on Wayland. CUPS and NSS are built only because the Chrome binary links against them.

Steam is Valve's launcher, as Arch packages it. The `lib32-*` recipes give it and 32-bit games the same Mesa drivers (with a 32-bit LLVM), the X11 and Vulkan libraries, ALSA through PipeWire, and NSS, all built from the same sources as the 64-bit versions. Steam still downloads its own runtime for games, as it does everywhere.

Firmware isn't held back for being non-free: linux-firmware (Wi-Fi, Bluetooth, GPUs), Intel's SOF audio firmware with Arch's full set of sound drivers (`kernel-sound.syn`), and Intel and AMD CPU microcode, built into the kernel because there's no initramfs to carry it.

## Not there yet

- Installing next to another OS, or onto an encrypted or LVM root. The installer takes the whole disk, and the kernel has no initramfs to unlock anything.
- SYN-SHARE (its rsync, Samba, NFS, HTTP, TFTP and netcat services are systemd units), and the BlackArch toggle and ISO builder, which are pacman and archiso.
- GTK's icons. Adwaita's cursors are in, but its icons are SVG, and drawing those needs librsvg, which needs Rust.
- qt6ct, so syn-filemanager doesn't follow the SYN theme yet. It needs Qt's translation tools (qttools).
- The Arch desktop's larger apps: FeatherPad, Falkon, VLC, GIMP, Audacity, OBS and the rest of `syn-packages.zsh`'s app list.
- zenity, which Steam uses for a few error dialogs. It needs GTK4 and libadwaita.
- Screen sharing from Chrome, which needs xdg-desktop-portal-wlr.
- A package manager. Every package is tracked in `/var/lib/syn-lfs/pkgs`, but nothing records which files it installed. jhalfs can build the base with pacman or dpkg underneath, which is the likely route.
