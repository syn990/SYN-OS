# SYN-LFS

SYN-OS built from source instead of from Arch packages. The base is Linux From Scratch (the SysV book, r13.1) built by jhalfs, with runit swapped in as PID 1. On top of that, `desktop/` builds the same labwc and waybar desktop the Arch ISO installs, plus Wi-Fi, sound and Google Chrome.

Everything happens in a 20G disk image under `~/SYN-LFS-build`. Nothing on the build machine's own disks or bootloader is touched. The kernel is one EFI-stub file with its command line built in, so there's no bootloader. It finds its root by partition label, so the same image boots in QEMU or written to a USB stick.

## Building it

From scratch, on an Arch (or SYN-OS) machine with jhalfs checked out in `~/SYN-LFS-build/jhalfs`:

```
./build.zsh image        # the disk image, partitioned and mounted
./build.zsh kernel       # kernel .config: defconfig + kernel.syn
./build.zsh configure
./build.zsh build        # LFS itself, a few hours
./build.zsh runit
./build.zsh fetch        # the desktop's sources, about 1.2G (half of it firmware)
./build.zsh desktop      # the desktop, another few hours (LLVM is most of it)
./build.zsh user yourname
./build.zsh esp
./build.zsh umount
./build.zsh gui          # or `boot` for just the serial console
```

Log in on tty1 and type `synos`, same as on Arch.

If LFS is already built from before the desktop existed, the kernel needs redoing for the new config (GPU, Wi-Fi and sound drivers, and root by label): `mount`, `kernel`, `rekernel`, then carry on from `fetch`.

`desktop` picks up where it stopped. A package that fails leaves its build tree in `/tmp/syn-build` and its log in `/var/log/syn-lfs` inside the image, and running `desktop` again starts from that package. `desktop NAME` rebuilds just that one.

## How the desktop is put together

Each package is a small recipe in `desktop/pkgs` (version, source URL, checksum, build commands), built in the order listed in `desktop/order`. Where BLFS has the package, the version, md5 and commands come from the BLFS r13.1 book, so they match the LFS toolchain. The rest (labwc, waybar, foot, fuzzel, iwd, rofi and so on) are pinned to their current releases with sha256 sums. Sources are downloaded on the host and the chroot builds offline.

Where it differs from the Arch build, because there's no systemd and no logind:

- seatd gives labwc its seat, and users in the `video` group can start a session. runit stage 1 creates `/run/user/<uid>` at boot.
- D-Bus, seatd, iwd and dhcpcd are runit services. iwd joins networks and dhcpcd gets the address, same split as on Arch.
- PipeWire, WirePlumber and pipewire-pulse are started from labwc's autostart (`syn-pipewire-session.sh`), since there are no user units to do it.
- mako and syn-connect talk D-Bus through basu, the standalone sd-bus library.
- Reboot and power off in the menus run runit's `reboot` and `poweroff` through doas.
- Chrome is Google's own .deb unpacked into `/opt`, running on Wayland. The CUPS, NSS and X11 client libraries are built only because the Chrome binary links against them.

## Not there yet

- Steam. Its client is 32-bit, so it needs either a multilib LFS (jhalfs can build one, but that means building the base again) or Flatpak, which brings its own 32-bit runtime and Mesa.
- Bluetooth, and sound on newer Intel laptops that need SOF firmware.
- Xwayland, so X11-only apps don't run yet.
- syn-filemanager and featherpad (both Qt), syn-relay (ffmpeg and SDL2) and syn-sysmon (reads the systemd journal).
- An icon theme. Adwaita needs librsvg, which needs Rust.
- A package manager. Every package is tracked in `/var/lib/syn-lfs/pkgs`, but nothing records which files it installed. jhalfs can build the base with pacman or dpkg underneath, which is the likely route.
