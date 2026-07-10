# SYN-OS

**A highly customisable, efficient Arch Linux build by William Hayward-Holland (Syntax990).**

This is for people who want to manage their own machine, not have it managed for them. No installer wizard, no app store, nothing hidden behind a GUI. It's Arch with an installer that does what I'd otherwise do by hand, a package list I've been arguing with myself over since 2021, and a desktop built from whatever was lean enough to keep around.

Boot the ISO, run one command, and you get a working system. Every file that built it is in this repository, so you can always go check why it behaves the way it does.

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

Plus a browser, media player, image editor, and whatever else made it into the package list (see [Package Collection](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/packages.md)). Every file on the installed system came from this repository. See [Philosophy](#philosophy).

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

See [How the Installer Works](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/installer-overview.md) for the full pipeline, or [synos.conf](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/synos-conf.md) for every config option.

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

Most distros make the choices for you and then hide where those choices live. This one doesn't — every package, config, and install step is a plain file in this repo, not something the installer decides on its own. Full argument, with the diagram, is in [Philosophy](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/philosophy.md).

---

## Build Your Own ISO

SYN-OS is a complete [ArchISO](https://wiki.archlinux.org/title/Archiso) profile. `archiso` is Arch-only, so you need an Arch (or Arch-based) environment to build from — that's either an installed SYN-OS/Arch system, or the live ISO shell itself, which is a full Arch environment before you've even installed anything.

**Already on an installed SYN-OS desktop:** open the root menu (`Super+Space`) → SYN-OS Tools → SYN-OS ISO Builder. First run clones this repo to `~/GithubProjects/SYN-OS` and launches the builder; no terminal needed.

**From a terminal, on Arch/SYN-OS (installed or live ISO shell):**

```bash
sudo pacman -S archiso grub git
git clone https://github.com/syn990/SYN-OS.git
cd SYN-OS/SYN-OS
sudo zsh ./BUILD-SYNOS-ISO.zsh
```

Output is written to `ISO_OUTPUT/*.iso`. Any changes you made to packages, configs, dotfiles, or installer logic will be reflected in the image. See [Building the ISO](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/building-the-iso.md) for full details.

**From a Windows or Mac host:** there's no native way to run `BUILD-SYNOS-ISO.zsh` on Windows or macOS. Boot the ISO you already downloaded (real hardware via USB, or straight off the ISO in any VM tool — Hyper-V, VirtualBox, UTM, Parallels) and use either path above from inside that live shell.

---

## Documentation

The sections above cover getting started. Everything below goes deeper: how the system actually works, what each component does, how to modify or extend it.

### The Installer

| Document | Description |
|---|---|
| [How the Installer Works](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/installer-overview.md) | End-to-end walkthrough of what happens when you run `synos-install` |
| [Stage 0: Pre-Chroot Setup](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/stage0.md) | Disk partitioning, volume setup, filesystem creation, pacstrap. Everything before `arch-chroot` |
| [Stage 1: In-Chroot Configuration](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/stage1.md) | Users, bootloader, services, dotfile deployment. Everything inside the new system |
| [synos.conf: Declarative Strategy Selection](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/synos-conf.md) | How to choose partition, volume, filesystem, and bootloader strategies without touching script logic |
| [Storage Strategies](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/storage-strategies.md) | LUKS encryption, LVM, F2FS, Btrfs, what each strategy does and when to use it |
| [Building the ISO](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/building-the-iso.md) | How to rebuild SYN-OS from source and make it your own |
| [Philosophy](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/philosophy.md) | Why decisions live in plain files, not installer logic, with the diagram |
| [Project History](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/history.md) | The real history, back to 2021. Actual file contents, diffs, and Graphviz diagrams, not a summary |

### Packages

| Document | Description |
|---|---|
| [Package Collection](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/packages.md) | Full breakdown of `syn-packages.zsh`: every category, every package, and why it is included |

### The Desktop

| Document | Description |
|---|---|
| [LabWC: Window Manager](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/labwc.md) | How LabWC works, key config files (`rc.xml`, `menu.xml`, `environment`), keybindings, and layout |
| [Waybar: The Panel](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/waybar.md) | Module structure, `config.jsonc`, `style.css`, and how to add or restyle modules |
| [Wayland vs X11: What Changed and Why](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/wayland.md) | Plain-English explanation of the display system, why SYN-OS moved to Wayland, and what that means in practice |
| [Zsh Configuration](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/zsh.md) | Shell setup, aliases, plugins (autosuggestions, syntax highlighting, fzf, zoxide) |
| [Dotfile Overlay](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/dotfile-overlay.md) | How `DotfileOverlay/` works, what gets deployed where, and how to customise defaults |

### Concepts

| Document | Description |
|---|---|
| [What is a Window Manager?](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/window-manager.md) | The difference between a desktop environment, window manager, and compositor, explained plainly |
| [What is Wayland?](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/wayland.md) | How the Linux display system works and why it matters |
| [What is a Shell?](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/shell.md) | TTY, terminal emulator, shell, prompt: what each layer actually is |
| [What is Arch Linux?](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/arch-linux.md) | The base SYN-OS is built on: rolling release, pacman, AUR, and the Arch philosophy |
| [Filesystem Hierarchy](./SYN-OS/SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/concepts/filesystem.md) | What `/etc`, `/usr`, `/home`, `/mnt` and the rest of the Linux directory tree actually mean |

---

## License

MIT, see [LICENSE](LICENSE).

## Contact

- **Email:** william@npc.syntax990.com
- **LinkedIn:** [William Hayward-Holland](https://www.linkedin.com/in/william-hayward-holland-990/)
- **Arch Wiki:** [wiki.archlinux.org](https://wiki.archlinux.org), invaluable reference for everything under the hood
