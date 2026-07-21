# SYN-OS

A hand-built Arch Linux desktop by William Hayward-Holland (Syntax990).

![SYN-OS Desktop](./Images/labwc-SYNOS-1.png)

No desktop environment. No bloat. No config layer you have to fight. Just
one installer, one theme system, and a set of tools built to do exactly
what's needed and nothing more. Everything's dark, fast, and Wayland
native. The menu always shows what's really on your system, not a fixed
list somebody forgot to update.

---

## Download and install

[**Download SYN-OS (~1.1 GB)**](https://drive.google.com/file/d/1cM35ZwfR67CDV1SkpdlipEFGwd2FL3-2/view?usp=sharing)

```bash
lsblk                          # find your USB, e.g. /dev/sdb
sudo dd if=SYN-OS.iso of=/dev/sdX bs=4M status=progress oflag=sync
```

macOS: `diskutil unmountDisk /dev/diskN` then `sudo dd if=SYN-OS.iso of=/dev/rdiskN bs=4m`.
Windows: use [Rufus](https://rufus.ie/). GPT for UEFI, MBR for BIOS.

Boot the USB and pick SYN-OS. You'll land in a live shell. Set your
preferences, then install:

```bash
synos-config    # quick interactive picker for every setting
synos-install
```

Reboot, log in, run `synos` to start the desktop. Full walkthrough here: [How installing SYN-OS works](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/installer-overview.md).

---

## Build your own ISO

You'll need an Arch environment. Either an installed SYN-OS/Arch system, or
the live ISO shell itself.

```bash
sudo pacman -S archiso git
git clone https://github.com/syn990/SYN-OS.git
cd SYN-OS
sudo zsh ./BUILD-ARCHISO.zsh
```

The finished ISO lands in `ISO_OUTPUT/*.iso`. From an installed desktop you
can also just open `Super+Space` → SYN-OS Tools → ISO Builder, no terminal
needed. Windows and macOS can't run the build script directly, so boot the
downloaded ISO in a VM and build from inside that. More detail: [Building your own ISO](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/build/iso-builder.md).

---

## Documentation

Every doc below is also right there on the installed system: `Super+Space`
→ Docs. The desktop explains itself, no browser needed.

| Area | Docs |
|---|---|
| **Getting started** | [How installing SYN-OS works](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/installer-overview.md) · [Choosing your setup (synos.conf)](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/synos-conf.md) · [Disk & storage options](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/storage-strategies.md) |
| **What's included** | [The package collection](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/packages.md) |
| **The desktop** | [The window manager (LabWC)](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/labwc.md) · [The top bar (Waybar)](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/waybar.md) · [How your settings are set up](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/dotfile-overlay.md) · [The shell](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/zsh.md) · [Why Wayland, not X11](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/wayland.md) |
| **Look and feel** | [The theme system](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/theming/theme-engine.md) · [Theme gallery](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/theming/theme-gallery.md) |
| **Built-in tools** | [File manager](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/syn-filemanager.md) · [File sharing](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/syn-share.md) · [Encryption](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/syn-crypter.md) · [Directory maps](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/syn-graphmap.md) · [Wi-Fi](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/wifi.md) · [Audio mixer](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/audio.md) · [Display & screens](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/display.md) · [System monitor & logs](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/syn-sysmon.md) · [Screenshots & recording](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/screenshot-and-recording.md) · [Services](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/services-toggle.md) · [BlackArch tools](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/blackarch-toggle.md) · [Notifications](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/notifications.md) |
| **Building it yourself** | [Building your own ISO](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/build/iso-builder.md) |
| **Background** | [Why SYN-OS exists](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/philosophy.md) · [Project history](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/history.md) |
| **New to Linux terms?** | [What's a window manager](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/window-manager.md) · [What's Wayland](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/wayland.md) · [What's a shell](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/shell.md) · [What's Arch Linux](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/arch-linux.md) · [How Linux organizes files](./SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/filesystem.md) |

---

## License

MIT, see [LICENSE](LICENSE).

## Contact

- **Email:** william@npc.syntax990.com
- **LinkedIn:** [William Hayward-Holland](https://www.linkedin.com/in/william-hayward-holland-990/)
- **Arch Wiki:** [wiki.archlinux.org](https://wiki.archlinux.org)
