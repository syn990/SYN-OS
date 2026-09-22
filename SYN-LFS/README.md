# SYN-LFS

SYN-OS built from source instead of from Arch packages. The base is Linux From Scratch built by jhalfs, from the SysV multilib book: the same packages as LFS 13.1, plus 32-bit copies of the core libraries next to the 64-bit ones, which Steam needs. runit replaces sysvinit as PID 1. On top of that, `desktop/` builds the same labwc and waybar desktop the Arch ISO installs, plus Wi-Fi, sound, Xwayland, Google Chrome and Steam.

Everything happens in a 20G disk image under `~/SYN-LFS-build`. Nothing on the build machine's own disks or bootloader is touched. The kernel is one EFI-stub file with its command line built in, so there's no bootloader. It finds its root by partition label, so the same image boots in QEMU or written to a USB stick.

## Building it

On an Arch (or SYN-OS) machine with jhalfs checked out in `~/SYN-LFS-build/jhalfs`:

```
./build.zsh image        # the disk image, partitioned and mounted
./build.zsh kernel       # kernel .config: defconfig + kernel.syn
./build.zsh configure
./build.zsh build        # LFS itself, a few hours
./build.zsh runit
./build.zsh fetch        # the desktop's sources, about 1.3G (half of it firmware)
./build.zsh desktop      # the desktop, several hours (the two LLVMs are most of it)
./build.zsh user yourname
./build.zsh esp
./build.zsh umount
./build.zsh gui          # or `boot` for just the serial console
```

Log in on tty1 and type `synos`, same as on Arch. Steam is in the launcher, or run `steam` from a terminal. The first run downloads the client into `~/.local/share/Steam`.

An image built before the base went multilib has no 32-bit libraries, so it has to be built again: move `lfs.img` out of the way and start from `image`. Sources already fetched are reused.

If `kernel.syn` changes after LFS is built, `mount`, `kernel`, `rekernel` and `esp` rebuild just the kernel.

`desktop` picks up where it stopped. A package that fails leaves its build tree in `/tmp/syn-build` and its log in `/var/log/syn-lfs` inside the image, and running `desktop` again starts from that package. `desktop NAME` rebuilds just that one.

## How the desktop is put together

Each package is a small recipe in `desktop/pkgs` (version, source URL, checksum, build commands), built in the order listed in `desktop/order`. Where BLFS has the package, the version, md5 and commands come from the BLFS r13.1 book, so they match the LFS toolchain. The rest (labwc, waybar, foot, fuzzel, iwd, rofi, Steam and so on) are pinned to their current releases with sha256 sums. Sources are downloaded on the host and the chroot builds offline.

Where it differs from the Arch build, because there's no systemd and no logind:

- seatd gives labwc its seat, and users in the `video` group can start a session. runit stage 1 creates `/run/user/<uid>` at boot.
- D-Bus, seatd, iwd and dhcpcd are runit services. iwd joins networks and dhcpcd gets the address, same split as on Arch.
- PipeWire, WirePlumber and pipewire-pulse are started from labwc's autostart (`syn-pipewire-session.sh`), since there are no user units to do it.
- mako and syn-connect talk D-Bus through basu, the standalone sd-bus library.
- Reboot and power off in the menus run runit's `reboot` and `poweroff` through doas.
- Steam's controller rules normally give devices to whoever is logged in through logind. Here they go to the `input` group instead, which `user` puts you in.
- There's no PAM limits file, so runit stage 2 raises the open-files hard limit for every login (Proton's esync wants it).

Chrome is Google's own .deb unpacked into `/opt`, running on Wayland. CUPS and NSS are built only because the Chrome binary links against them.

Steam is Valve's launcher, as Arch packages it. The `lib32-*` recipes give it and 32-bit games the same Mesa drivers (with a 32-bit LLVM), the X11 and Vulkan libraries, ALSA through PipeWire, and NSS, all built from the same sources as the 64-bit versions. Steam still downloads its own runtime for games, as it does everywhere.

## Not there yet

- Bluetooth, and sound on newer Intel laptops that need SOF firmware.
- syn-filemanager and featherpad (both Qt), syn-relay (ffmpeg and SDL2) and syn-sysmon (reads the systemd journal).
- An icon theme. Adwaita needs librsvg, which needs Rust.
- zenity, which Steam uses for a few error dialogs. It needs GTK4 and libadwaita.
- A package manager. Every package is tracked in `/var/lib/syn-lfs/pkgs`, but nothing records which files it installed. jhalfs can build the base with pacman or dpkg underneath, which is the likely route.
