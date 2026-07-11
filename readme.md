# SYN-OS

**William Hayward-Holland's (Syntax990) Arch Linux build.**

This isn't a distro trying to be a product. It's Arch with an installer that does what I'd otherwise do by hand, a package list I've been arguing with myself over since 2021, and a desktop built out of whatever was lean enough to justify keeping around. No installer wizard, no app store, nothing hidden behind a GUI that you can't also just go read.

Boot the ISO, run one command, and you get a working system where every file that put it together is sitting right there in this repository if you want to know why it behaves the way it does.

![SYN-OS Banner](./Images/SYN-BANNER1.png)

Once it's installed, this is what you're looking at:

| Component | What it does |
|---|---|
| **LabWC** | Wayland window manager/compositor. Lightweight, keyboard-friendly, Openbox-style behaviour |
| **Waybar** | Top panel: clock, system stats, workspaces, custom modules, fully scriptable and CSS-styled |
| **Swaybg** | Sets the desktop wallpaper |
| **Wmenu** | Application launcher. Press the top-left icon, type a name or path, run things |
| **Foot** | Wayland terminal emulator, your main interface for most tasks |
| **spf (Superfile)** | Terminal-based file manager |
| **Zsh** | Shell with autosuggestions, syntax highlighting, and fuzzy search built in |

Plus a browser, media player, image editor, and whatever else made it into the package list (see [Package Collection](./docs/packages.md)). Every file on the installed system came from this repository, no exceptions. See [Philosophy](#philosophy).

![SYN-OS Desktop](./Images/labwc-SYNOS-1.png)

---

## Download

[**Download SYN-OS (~1.1 GB)**](https://drive.google.com/file/d/13CowFj1Pwo4XzBRVkGT-cBjKuVWJ50cW/view?usp=sharing)

---

## Create a Bootable USB

You need to write the ISO image onto a USB stick so your computer can boot from it.

**Linux**
```bash
lsblk                          # lists your drives, find your USB (e.g. /dev/sdb)
sudo dd if=SYN-OS.iso of=/dev/sdX bs=4M status=progress oflag=sync
```
Replace `sdX` with your USB device, not a partition like `sdX1`.

**macOS**
```bash
diskutil list                  # find your USB disk number
diskutil unmountDisk /dev/diskN
sudo dd if=SYN-OS.iso of=/dev/rdiskN bs=4m
sync
diskutil eject /dev/diskN
```

**Windows**, use [Rufus](https://rufus.ie/):
1. Insert USB → open Rufus → select the ISO
2. Partition scheme: **GPT** for modern (UEFI) systems, **MBR** for older (BIOS) systems
3. Click **Start**

---

## Install

1. Boot your machine or VM from the USB stick
2. Select **SYN-OS** from the boot menu
3. You will land in a terminal. This is the live environment
4. *(Optional)* Inspect or adjust the installer config before starting:
   ```bash
   nano /etc/syn-os/synos.conf
   ```
5. Run the installer:
   ```bash
   synos-install
   ```
6. Follow the prompts. The two-stage installer handles everything:
   - Disk partitioning and encryption
   - Filesystem creation
   - Package installation
   - Config and dotfile deployment
   - Bootloader setup

See [How the Installer Works](./docs/installer-overview.md) for the full pipeline, or [synos.conf](./docs/synos-conf.md) for every config option.

---

## First Boot

Remove the USB, reboot, and log in with the account created during installation.

Launch the desktop:
```bash
synos
```

This starts the **LabWC** Wayland session. Your panel, wallpaper, menus, and applications are all ready.

---

## Philosophy

Most distros make the choices for you and then hide where those choices live. This one doesn't, mostly because I never wanted to have to reverse-engineer my own system six months later.

Every package is in [`syn-packages.zsh`](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/syn-packages.zsh), commented, in plain arrays. Every config ships from [`DotfileOverlay/`](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/DotfileOverlay/) and is just the file, not a template that generates one. The install runs through named stage scripts you can open and read start to finish. If something behaves oddly, the file that caused it is in this repo somewhere, and now there's [documentation](#documentation) too, for the parts that aren't obvious from the code alone.

I've rebuilt this system from nothing more times than I'd like to admit, going back to before this repository existed, before any repository existed. [Project History](./docs/history.md) has the real version of that, sourced from the actual commits and file contents across both the current repo and the one before it, not a tidied-up summary.

---

## Build Your Own ISO

SYN-OS is a complete [ArchISO](https://wiki.archlinux.org/title/Archiso) profile. If you are on Arch Linux or any Arch-based system, you can rebuild the ISO yourself:

```bash
sudo pacman -S archiso grub git
git clone https://github.com/syn990/SYN-OS.git
cd SYN-OS/SYN-OS
sudo zsh ./BUILD-SYNOS-ISO.zsh
```

Output is written to `ISO_OUTPUT/*.iso`. Any changes you made to packages, configs, dotfiles, or installer logic will be reflected in the image. See [Building the ISO](./docs/building-the-iso.md) for full details.

---

## Documentation

The sections above cover getting started. Everything below goes deeper: how the system actually works, what each component does, how to modify or extend it.

### The Installer

| Document | Description |
|---|---|
| [How the Installer Works](./docs/installer-overview.md) | End-to-end walkthrough of what happens when you run `synos-install` |
| [Stage 0: Pre-Chroot Setup](./docs/stage0.md) | Disk partitioning, volume setup, filesystem creation, pacstrap. Everything before `arch-chroot` |
| [Stage 1: In-Chroot Configuration](./docs/stage1.md) | Users, bootloader, services, dotfile deployment. Everything inside the new system |
| [synos.conf: Declarative Strategy Selection](./docs/synos-conf.md) | How to choose partition, volume, filesystem, and bootloader strategies without touching script logic |
| [Storage Strategies](./docs/storage-strategies.md) | LUKS encryption, LVM, F2FS, Btrfs, what each strategy does and when to use it |
| [Building the ISO](./docs/building-the-iso.md) | How to rebuild SYN-OS from source and make it your own |
| [Project History](./docs/history.md) | The real history, back to 2021. Actual file contents, diffs, and Graphviz diagrams, not a summary |

### Packages

| Document | Description |
|---|---|
| [Package Collection](./docs/packages.md) | Full breakdown of `syn-packages.zsh`: every category, every package, and why it is included |

### The Desktop

| Document | Description |
|---|---|
| [LabWC: Window Manager](./docs/labwc.md) | How LabWC works, key config files (`rc.xml`, `menu.xml`, `environment`), keybindings, and layout |
| [Waybar: The Panel](./docs/waybar.md) | Module structure, `config.jsonc`, `style.css`, and how to add or restyle modules |
| [Wayland vs X11: What Changed and Why](./docs/wayland.md) | Plain-English explanation of the display system, why SYN-OS moved to Wayland, and what that means in practice |
| [Zsh Configuration](./docs/zsh.md) | Shell setup, aliases, plugins (autosuggestions, syntax highlighting, fzf, zoxide) |
| [Dotfile Overlay](./docs/dotfile-overlay.md) | How `DotfileOverlay/` works, what gets deployed where, and how to customise defaults |

### Concepts

| Document | Description |
|---|---|
| [What is a Window Manager?](./docs/concepts/window-manager.md) | The difference between a desktop environment, window manager, and compositor, explained plainly |
| [What is Wayland?](./docs/concepts/wayland.md) | How the Linux display system works and why it matters |
| [What is a Shell?](./docs/concepts/shell.md) | TTY, terminal emulator, shell, prompt: what each layer actually is |
| [What is Arch Linux?](./docs/concepts/arch-linux.md) | The base SYN-OS is built on: rolling release, pacman, AUR, and the Arch philosophy |
| [Filesystem Hierarchy](./docs/concepts/filesystem.md) | What `/etc`, `/usr`, `/home`, `/mnt` and the rest of the Linux directory tree actually mean |

---

## License

MIT, see [LICENSE](LICENSE).

## Contact

- **Email:** william@npc.syntax990.com
- **LinkedIn:** [William Hayward-Holland](https://www.linkedin.com/in/william-hayward-holland-990/)
- **Arch Wiki:** [wiki.archlinux.org](https://wiki.archlinux.org), invaluable reference for everything under the hood
