# SYN-OS on runit

SYN-OS built from source: Linux From Scratch as the base, runit as PID 1 and zsh in the base, then the SYN desktop (labwc, waybar, foot, the SYN tools), Wi-Fi, sound, surf, Falkon and Steam on top. The result is a disk image and an installer ISO.

Everything lands in a sparse 100G disk image (`SYN_OS_IMGSIZE` changes it; `grow` enlarges an existing one) under `~/SYN-OS-build` (`SYN_OS_WORK` moves it). Nothing on the build machine's disks or bootloader is touched.

## Build it

On an Arch machine with 16G+ of RAM and 60G free:

```
git clone https://github.com/syn990/SYN-OS.git
cd SYN-OS/SYN-LFS
./build.zsh host     # packages, doas, jhalfs (once per machine)
./build.zsh all      # the lot: LFS, the desktop, the ISO
```

`all` takes the best part of a day, most of it in LLVM, Qt, QtWebEngine and WebKitGTK. It records each finished step in `~/SYN-OS-build/build.state` and logs to `build.log`, so if a step fails you fix it and run `all` again: it carries on from there. `desktop` does the same per package, and `desktop NAME` rebuilds one.

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

Log in and type `synos` to start the desktop. `build.zsh` with no arguments lists every step, for running them one at a time. If `kernel.syn` changes after the base is built, `mount`, `kernel`, `rekernel` and `esp` rebuild just the kernel. The VM and the build both write the image: shut the VM down before any step that mounts it.

## What gets built

The base is LFS from the SysV multilib book (LFS 13.1's packages, plus i686 libraries for Steam), built by jhalfs, the LFS project's own automation. The book's commit is pinned in `build.zsh`.

The kernel is a single EFI-stub file with its command line built in, installed where the firmware looks for a boot file. No bootloader, no initramfs, and it finds its root by partition label (`root=PARTLABEL=synroot`), so the same file boots in QEMU, off a disk or off a USB stick. `kernel.syn` holds the options that matter (disks, GPUs, Wi-Fi, Bluetooth, input, Steam's controllers and ntsync); `kernel-sound.syn` is Arch's sound configuration.

Firmware isn't held back for being non-free: linux-firmware, Intel's SOF audio firmware, and Intel and AMD microcode, built into the kernel since there's no initramfs to carry it.

The desktop's applications come from the same list the Arch ISO installs, `syn-packages.zsh`: VLC, GIMP, OBS, FeatherPad, lxqt-archiver, feh, the CLI tools, the filesystem and disk tools, OpenVPN, Bluetooth, and so on. Audacity 4 is built from source with its muse_deps libraries taken from the system, and OpenRA is built from source on .NET 10 (Microsoft's SDK, as Rust is upstream's build), its NuGet packages fetched with the other sources. The Rust toolchain is upstream's build, there for librsvg, which draws SVG icons, thumbnails and the diagrams in the Docs menu. fzf, zoxide, ripgrep, fd, bat and glow are upstream's prebuilt release binaries.

surf is the browser that starts in seconds: suckless surf on WebKitGTK, with GStreamer (plugins base, good and bad, and gst-libav for FFmpeg's decoders) for sound and video. Falkon is built too, with QtWebEngine under it, which is Chromium as a Qt library: hours of compiling and tens of gigabytes of build tree, so it comes late in the desktop step. Steam is Valve's launcher; the `lib32-*` recipes give it and 32-bit games Mesa (with a 32-bit LLVM), the X11 and Vulkan libraries and ALSA through PipeWire. Steam still downloads its own runtime, as it does everywhere.

## Profiles

`desktop/order` lists the recipes in build order, each after everything it needs. A `# [category]` header puts the recipes under it in a category, until the next header. Two lines at the top declare the profiles as sets of categories:

```
profile full    baseCore netAndServices shellAndCLI desktopStack fontsI18n appsMedia synOS
profile minimal baseCore netAndServices shellAndCLI desktopStack fontsI18n synOS
```

A profile filters the sequence and never reorders it. `minimal` (209 recipes) is a desktop that boots: everything but `appsMedia`. `full` (309) adds `appsMedia`: the browsers, VLC, GIMP, OBS and the rest, and Steam with its 32-bit tree. `SYN_OS_PROFILE=full|minimal` picks one for `fetch` and `desktop` (default `full`); `full` after a `minimal` build builds only the difference. `zsh desktop/syn-pkg.zsh list` prints a profile's recipes.

## Recipes

A recipe in `desktop/pkgs/NAME` sets `v` (the version), `src` (one `"url checksum [filename]"` per file; the first is unpacked, the rest sit next to it) and `build()`, which runs as root inside the unpacked source with `lib.zsh` loaded and `err_exit`, `pipe_fail` and `sh_word_split` on. Where BLFS r13.1 has the package, the version, md5 and commands are the book's; everything else is pinned to a release with a sha256, or `-` for the files with no fixed release. `noextract=1` leaves the file packed; `always=1` rebuilds every run (the SYN tools, built from this repo); `keep=1` reuses a stopped build tree.

`lib.zsh` gives recipes `meson_build` and `cmake_build`; `configure32`, `meson_build32` and `cmake_build32` for the 32-bit builds; `svc NAME [off]`, a runit service with an svlogd log, enabled unless `off`; and `syn_tool NAME`, a SYN-SOFTWARE tool built with CMake.

Sources are downloaded on the host into `~/SYN-OS-build/sources/desktop` and the chroot builds offline: it sees them at `/sources/syn-desktop`, and this repo read-only at `/usr/src/SYN-OS`. A package that fails leaves its build tree in `/tmp/syn-build` and its log in `/var/log/syn-os` inside the image. Built versions are recorded in `/var/lib/syn-os/pkgs`.

## Running without systemd

runit is PID 1, and there's no logind, so:

- Stage 1 (`runit/1`) mounts the kernel filesystems, coldplugs udev, checks and mounts fstab, sets up zram from `synos.conf`'s `ZramPercent` and `ZramMaxMiB` (the job systemd's zram-generator does on Arch), makes `/run/user/<uid>`, and loads the console keymap and font. The kernel sets the clock from the RTC itself; there is no hwclock at boot, which waited 10 s for a clock tick under QEMU.
- seatd hands labwc the seat. Users in `video` can start a session.
- D-Bus, seatd, iwd, dhcpcd and ntpd are services; sshd and bluetoothd are installed but switched off. Preferences > Software > Services toggles any of them. Each service logs through svlogd to `/var/log/sv/NAME`.
- dhcpcd configures every interface; the book's static `ifconfig.*` files are removed.
- PipeWire, WirePlumber and pipewire-pulse start from labwc's autostart, since there are no user units.
- mako and syn-connect use basu for sd-bus.
- Reboot and power off run runit's own, through doas.
- Steam's controller rules hand devices to the `input` group rather than to whoever logind says is here.
- runit stage 2 raises the open-files limit for every login, which Proton's esync needs.

The dotfiles come from `SYN-ISO-PROFILE`, with the `syn-desktop` recipe making the changes this system needs: its own `menu.xml` (surf and Steam up front, All Applications generated from the installed .desktop files, the runit services toggle), no SYN-SHARE module on the bar, the Adwaita cursor, dark GTK, and `xdg-open` pointed at surf, FeatherPad and syn-filemanager. On QEMU's virtio GPU, labwc draws the cursor itself (`WLR_NO_HARDWARE_CURSORS`), since the hardware cursor shows upside down there. syn-sysmon's Logs view reads the service logs and syslog rather than a journal.

## The installer ISO

`iso` runs after `desktop`, `kernel` and `rekernel` (the kernel needs the live-boot options and the microcode). It makes `~/SYN-OS-build/syn-os-DATE.iso`: the system as a squashfs (without `/sources`, the jhalfs tree, the build logs, `/home`, `/root` and any account added with `user`), the kernel, an initramfs holding `iso/init` and the programs it runs, and GRUB with the SYN-OS splash. Burn it or `dd` it onto a USB stick.

The initramfs finds the medium labelled `SYN_OS`, mounts the squashfs read-only under a tmpfs overlay, lays `/usr/share/syn-os/live/` over it and hands over to runit. `syn.toram` on the kernel command line copies the image into RAM first, so the stick can come out. The live session logs `synstigator` in on tty1 (password `synstigator`, doas without a password); `synos` starts the desktop from there. Nothing done in the live session reaches the installed disk.

`synos-install` takes its answers from the first of these it finds: a file named on its command line; a punchset; the ISO's own `/etc/syn-os/synos.conf` (`synos-config` opens it in nano). A punchset is a medium labelled `SYNPUNCH` with a filled-in `synos.conf` at its root (`mkfs.vfat -n SYNPUNCH /dev/sdX`, copy the file on): plug it in and the install reads it. Whatever the file leaves at `CHANGE_ME` (`Disk`, `UserAccountPassword`) is asked for on the terminal. The boot menu's "Install SYN-OS from a punchset" entry starts the install by itself once the live session is up; with `RequireWipeConfirm=no` in the punchset nothing is asked at all. It wipes `Disk`, makes a GPT with a FAT32 EFI system partition (`SYNEFI`) and an ext4 root (`synroot`), copies the clean squashfs across, puts the kernel at `EFI/BOOT/BOOTX64.EFI`, and sets the hostname, time zone, keyboard, locale and user from `synos.conf`. Nothing is downloaded. The ISO boots under BIOS too; installing needs UEFI.

## Not there yet

- Installing next to another OS, or onto an encrypted or LVM root. The installer takes the whole disk, and there's no initramfs to unlock anything. cryptsetup and LVM are installed, so a second disk can be unlocked by hand.
- SYN-SHARE: its rsync, Samba, NFS, HTTP, TFTP and netcat services are systemd units.
- qt5ct and kvantum-qt5. Nothing here is built against Qt5; the Qt6 versions of both are in.
- zenity, which Steam uses for some dialogs (GTK4 and libadwaita).
- Screen sharing out of a browser, which needs xdg-desktop-portal-wlr.
- A package manager. Packages are recorded in `/var/lib/syn-os/pkgs`, but nothing tracks which files each one installed.
