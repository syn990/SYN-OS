# SYN-LFS

SYN-OS built from source: Linux From Scratch as the base, runit as PID 1, then the SYN desktop (labwc, waybar, foot, the SYN tools), Wi-Fi, sound, Chrome and Steam on top. The result is a disk image and an installer ISO.

Everything lands in `~/SYN-LFS-build`. Nothing on the build machine's disks or bootloader is touched.

## Build it

On an Arch machine with 16G+ of RAM and 60G free:

```
git clone https://github.com/syn990/SYN-OS.git
cd SYN-OS/SYN-LFS
./build.zsh host     # packages, doas, jhalfs (once per machine)
./build.zsh all      # the lot: LFS, the desktop, the ISO
```

`all` takes the best part of a day, most of it in LLVM, Qt and Chrome's dependencies. It records each finished step in `~/SYN-LFS-build/build.state` and logs to `build.log`, so if a step fails you fix it and run `all` again: it carries on from there rather than starting over. `desktop` on its own does the same per package, and `desktop NAME` rebuilds one.

Then boot it:

```
./build.zsh isotest        # boot the ISO in QEMU, with a blank disk to install onto
./build.zsh isotest disk   # boot that disk afterwards
```

Or skip the ISO and use the build image itself:

```
./build.zsh user yourname
./build.zsh esp
./build.zsh umount
./build.zsh gui
```

Log in and type `synos` to start the desktop. `build.zsh` with no arguments lists every step, for running them one at a time.

## What gets built

The base is LFS from the SysV multilib book (LFS 13.1's packages, plus i686 libraries for Steam), built by jhalfs, the LFS project's own automation. The book's commit is pinned in `build.zsh`.

The kernel is a single EFI-stub file with its command line built in, installed where the firmware looks for a boot file. No bootloader, no initramfs, and it finds its root by partition label, so the same file boots in QEMU, off a disk or off a USB stick. `kernel.syn` holds the options that matter (disks, GPUs, Wi-Fi, Bluetooth, input, Steam's controllers and ntsync); `kernel-sound.syn` is Arch's sound configuration.

The desktop is one small recipe per package in `desktop/pkgs`: version, source URL, checksum, build commands. `desktop/order` is the build order. Where BLFS has a package, the version, md5 and commands come from the book, so they match the LFS toolchain; everything else is pinned to a release with a sha256. Sources are fetched on the host and the chroot builds offline.

Firmware isn't held back for being non-free: linux-firmware, Intel's SOF audio firmware, and Intel and AMD microcode, built into the kernel since there's no initramfs to carry it.

Chrome is Google's own .deb unpacked into `/opt`. Steam is Valve's launcher; the `lib32-*` recipes give it and 32-bit games Mesa (with a 32-bit LLVM), the X11 and Vulkan libraries and ALSA through PipeWire. Steam still downloads its own runtime, as it does everywhere.

## Running without systemd

runit is PID 1, and there's no logind, so:

- seatd hands labwc the seat. Users in `video` can start a session; runit stage 1 makes `/run/user/<uid>`.
- D-Bus, seatd, iwd, dhcpcd and ntpd are services; sshd and bluetoothd are installed but switched off. Preferences > Software > Services toggles any of them. Each service logs through svlogd to `/var/log/sv/NAME`.
- PipeWire, WirePlumber and pipewire-pulse start from labwc's autostart, since there are no user units.
- mako and syn-connect use basu for sd-bus.
- Reboot and power off run runit's own, through doas.
- Steam's controller rules hand devices to the `input` group rather than to whoever logind says is here.
- runit stage 2 raises the open-files limit for every login, which Proton's esync needs.

The dotfiles come from `SYN-ISO-PROFILE`, with the `syn-desktop` recipe making the changes this system needs: its own `menu.xml` (Chrome and Steam up front, All Applications generated from the installed .desktop files, config files in nano, the runit services toggle), no SYN-SHARE module on the bar, the Adwaita cursor, dark GTK, and `xdg-open` pointed at Chrome, nano and syn-filemanager. syn-sysmon's Logs view reads the service logs and syslog rather than a journal.

## Not there yet

- Installing next to another OS, or onto an encrypted or LVM root. The installer takes the whole disk, and there's no initramfs to unlock anything.
- SYN-SHARE: its rsync, Samba, NFS, HTTP, TFTP and netcat services are systemd units.
- GTK's icons. The Adwaita cursors are in, but the icons are SVG and drawing those needs librsvg, which needs Rust.
- qt6ct, so syn-filemanager doesn't follow the SYN theme. It needs Qt's translation tools.
- The bigger apps from the Arch package list: FeatherPad, Falkon, VLC, GIMP, Audacity, OBS.
- zenity, which Steam uses for a few error dialogs. It needs GTK4 and libadwaita.
- Screen sharing out of Chrome, which needs xdg-desktop-portal-wlr.
- A package manager. Packages are recorded in `/var/lib/syn-lfs/pkgs`, but nothing tracks which files each one installed.
