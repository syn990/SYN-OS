# SYN-OS


**SYN-OS** is a highly customizable Arch Linux–based operating system by **William Hayward-Holland** (*Syntax990*).

It combines Arch’s minimal base with curated build scripts, dotfiles, and system overlays to deliver a clean, modular environment with full user control.

SYN-OS is designed for those who want:
- **Complete control** over packages, configs, and theming.
- **A terminal-first workflow**, with an optional lightweight Wayland session.
- **A transparent base system**, staying fully aligned with Arch principles.

> **Note:** The live installer runs entirely in a command-line (CLI) environment. Once installed, you may launch the graphical session using **LabWC**, a lightweight Wayland window manager that provides a simple Openbox‑like workflow with modern Wayland support.  
> The included **Waybar** panel uses JSON + CSS for straightforward module, layout, and theme customization.

![SYN-OS Banner](./Images/SYN-BANNER1.png)

## Download & Quick Start

**Latest Release:**  
- **Name:** SYN-OS SYNAPTICS Edition
- **Size:** ~1.1 GB  
- **Download:** [SYN-OS SYNAPTICS (FEB 2026).iso](https://drive.google.com/file/d/13CowFj1Pwo4XzBRVkGT-cBjKuVWJ50cW/view?usp=sharing)

**Older Releases:**
- [SYN-OS XENITH (JAN 2026).iso](https://drive.google.com/file/d/1bbKsw2FQ7d2Pb8Os1lwERGEyG5j3pnpg/view?usp=sharing)
- [SYN-OS SYNTEX (April 2025).iso](https://drive.google.com/file/d/1CcPMeKCBjdqz6OJCzm1JcLhxzKSHe7ra/view?usp=sharing)
- [SYN-OS M-141 (Nov 2024)](https://drive.google.com/file/d/1oX-hyHrG4M2JqXwFH2p5DxjbFT656jWH/view?usp=sharing)  
- [ArchTech Corp. Edition (Jul 2024)](https://drive.google.com/file/d/1WRDf0JfCCNhYJJkFUXb3Xheb3YInys52/view?usp=sharing)  
- [VOLITION (Jun 2024)](https://drive.google.com/file/d/16ETNY4jlTK_UCGEwBxMTTFMn0Mf7rrTR/view?usp=sharing)  
- [Soam-Do-Huawei (May 2024)](https://drive.google.com/file/d/1bsa85uXRdrfxPydkVNI-oQnpGj4JmeQi/view?usp=sharing)  
- [Chronomorph (Feb 2024)](https://drive.google.com/file/d/142U6-w2CNOiL2jRPlHmfqcYTlEmTBXow/view?usp=drive_link)

---

### Create a Bootable USB

#### Linux
    lsblk                                   # Identify your USB device (e.g., /dev/sdX)
    sudo dd if=SYN-OS_SYNTEX_2025-04.iso of=/dev/sdX bs=4M status=progress oflag=sync
*(Replace `sdX` with your USB device — not a partition like `sdX1`.)*

#### macOS
    diskutil list
    diskutil unmountDisk /dev/diskN
    sudo dd if=SYN-OS_SYNTEX_2025-04.iso of=/dev/rdiskN bs=4m
    sync
    diskutil eject /dev/diskN
*(Replace `N` with your USB disk number.)*

#### Windows (Rufus)
1. Insert USB drive.  
2. Open [Rufus](https://rufus.ie/).  
3. Select device → choose ISO → set Partition Scheme:  
   - GPT for UEFI systems  
   - MBR for legacy BIOS  
4. Click **Start**.

---
### Boot & Install

1. Boot your system or VM from the prepared USB stick.  
2. Select **SYN‑OS** in the boot menu.  
3. The live environment loads into a clean shell.  
4. *(Optional)* You may inspect or modify the installer scripts before beginning:

        nano /root/syn-resources/scripts/syn-stage0.zsh
        nano /root/syn-resources/scripts/syn-stage1.zsh

5. Start the installer:

        syntax990

6. Follow the prompts. Stage 0 and Stage 1 will handle:  
   - disk partitioning  
   - filesystem creation  
   - package installation  
   - overlay merging  
   - bootloader configuration  

---

### First Boot After Install

- Remove the USB stick and reboot.  
- Log in with the user account you created during installation.  
- To start the graphical session:

        synos

This launches the default **LabWC** Wayland session, configured with:

- **Waybar** as the top panel (JSON modules + CSS styling)  
- **Swaybg** for background handling  
- **archlinux-xdg-menu** integration for dynamic application menus  
- A minimal, clean environment designed to be extended by the user

![LabWC Desktop](./Images/labwc-SYNOS-1.png)

### About the Desktop Configuration

**Waybar**  
- Configured via `~/.config/waybar/config.jsonc`  
- Styled using `~/.config/waybar/style.css`  
- Modules, spacing, colour schemes, and fonts can be modified easily

**LabWC**  
- Uses Openbox-style XML syntax  
- Key files:

```
~/.config/labwc/rc.xml       # keybindings, window behaviour, placement rules
~/.config/labwc/menu.xml     # menu definition (includes archlinux-xdg-menu)
~/.config/labwc/environment  # session-wide environment variables
```

LabWC’s menu system can incorporate automatically generated XDG menus from:

```
/etc/xdg/menus/archlinux-applications.menu
```

This keeps the application list synced with installed packages, without maintaining menu items manually.

---

## Package Collection

Packages in SYN‑OS are grouped into arrays within the installation scripts, organised by purpose for logical clarity.

| **Category**              | **Description**                                              | **Packages** |
|---------------------------|--------------------------------------------------------------|--------------|
| **Core System**           | Essential components required for a functional base system and modern CLI environment | base, base-devel, bat, linux, linux-firmware, archlinux-keyring, zsh, zsh-completions, zsh-syntax-highlighting, zsh-autosuggestions, fzf, zoxide, ripgrep, fd, sudo |
| **Services**              | Networking and system-level daemons                         | dhcpcd, dnsmasq, hostapd, iwd, reflector, openvpn |
| **Environment & Shell**   | Wayland-based desktop environment, theming, UI tools, and terminal utilities | labwc, wmenu, archlinux-xdg-menu, waybar, pavucontrol-qt, qt5ct, qt6ct, kvantum, kvantum-qt5, feh, kitty, inetutils, rofi, calc, swaybg |
| **User Applications**     | Common everyday system utilities                            | nano, git, htop, pcmanfm-qt, engrampa, kwrite, ranger |
| **Developer Tools**       | Tools for development, image building, debugging, and hardware analysis | gcc, fakeroot, android-tools, archiso, binwalk, hexedit, lshw, yt-dlp |
| **Fonts & Localization**  | UI fonts, multilingual support, and Nerd Font compatibility | terminus-font, ttf-bitstream-vera, ttf-dejavu, noto-fonts, noto-fonts-emoji, noto-fonts-cjk, ttf-liberation, ttf-terminus-nerd, otf-font-awesome |
| **Optional Features**     | Additional media and creative applications                   | vlc, audacity, obs-studio, chromium, gimp, kdenlive |

## Philosophy

SYN‑OS is built to be a transparent and predictable system. All behaviour is defined by shell scripts, package arrays, and staged overlay directories. Nothing is hidden behind wrappers or helpers — every file that ends up on the final system exists in this repository and is copied or generated during installation.

The goal is not to automate Arch installation for convenience, but to provide a clean, modular framework where every component is inspectable, replaceable, and easy to reason about.

---

### System Overview

#### ISO Environment
Booting the SYN‑OS ISO loads a minimal live system containing the `syntax990` command. Running it begins the installer and triggers the two‑stage setup.

---

### Stage 0 — Pre‑Chroot Setup
Executed directly from the live environment.

**Purpose:**  
Prepare the disk, create the target filesystem, install base packages, and assemble the new system structure.

**Stage 0 uses:**
- `scripts/syn-disk-config.zsh` — partitioning and formatting  
- `scripts/syn-packages.zsh` — category‑based package arrays  
- `overlays/` — filesystem content copied into the target root  
- `syn-resources/` — supporting files such as defaults, configs, and menu definitions  

**Main actions:**
1. Disk layout and filesystem creation  
2. Mounting the target root  
3. Installing packages from the defined arrays  
4. Copying overlay directories:  
   - `/etc/` base configuration  
   - `/root/` resources  
   - `skel/` user defaults (`~/.config/*`, labwc configs, waybar, qt5ct, pcmanfm‑qt, ranger, etc.)  
5. Preparing the environment for Stage 1

After Stage 0 completes, the script enters the new system via `arch-chroot`.

---

### Stage 1 — In‑Chroot Configuration
Stage 1 performs all configuration inside the target filesystem.

**Main actions:**
- Creating users and assigning shells  
- Enabling services  
- Applying localisation (locale, console, timezone)  
- Deploying desktop environment components (LabWC, waybar, swaybg, QT settings, etc.)  
- Applying dotfile overlays from `DotfileOverlay/` into the user environment  
- Bootloader installation (UEFI/BIOS detected automatically)  
- Final cleanup

---


### Directory Structure

SYN‑OS is built from two major filesystem roots:

1. **The live ISO filesystem** (`airootfs/`)  
   → This is what you boot into when running the SYN‑OS ISO.

2. **The post‑install overlay** (`DotfileOverlay/`)  
   → These files are merged into the target system during Stage 1
     (e.g., `/etc`, `/etc/skel`, `/usr/share`).

Both appear inside this repository and are copied intentionally by the installer.
Nothing is auto‑generated or hidden.

```
SYN-OS/
│
├─ BUILD-SYNOS-ISO.zsh                     # Builds the entire ISO (mkarchiso wrapper)
│
├─ SYN-ISO-PROFILE/                        # Full ArchISO profile
│   ├─ airootfs/                           # (A) LIVE ISO ROOT FILESYSTEM
│   │   ├─ etc/
│   │   │   ├─ hostname
│   │   │   ├─ locale.conf
│   │   │   ├─ localtime
│   │   │   ├─ mkinitcpio.conf.d/
│   │   │   ├─ mkinitcpio.d/
│   │   │   ├─ modprobe.d/
│   │   │   ├─ pacman.d/
│   │   │   ├─ ssh/
│   │   │   ├─ systemd/                   # Live services, overrides, networking
│   │   │   ├─ vconsole.conf
│   │   │   └─ xdg/
│   │   │
│   │   ├─ root/
│   │   │   └─ syn-resources/             # Installer + overlays copied into the ISO
│   │   │       ├─ scripts/               # syn-stage0, syn-stage1, disk, packages, etc.
│   │   │       └─ DotfileOverlay/        # (B) POST-INSTALL OVERLAY ROOT
│   │   │           ├─ etc/               # -> becomes /etc on installed system
│   │   │           │   ├─ motd
│   │   │           │   ├─ os-release
│   │   │           │   ├─ skel/          # -> becomes /etc/skel
│   │   │           │   │   └─ .config/
│   │   │           │   │       ├─ htop/
│   │   │           │   │       ├─ kitty/
│   │   │           │   │       ├─ labwc/
│   │   │           │   │       ├─ pcmanfm-qt/
│   │   │           │   │       ├─ qt5ct/
│   │   │           │   │       ├─ ranger/
│   │   │           │   │       └─ waybar/
│   │   │           │   └─ vconsole.conf
│   │   │           │
│   │   │           └─ usr/
│   │   │               └─ share/
│   │   │                   └─ themes/    # Legacy/Openbox themes retained for users
│   │   │                       ├─ Retro 1 (Terminal)
│   │   │                       ├─ Retro 5 (Classic 98) ObiWine
│   │   │                       ├─ SYN-RTOS
│   │   │                       ├─ SYN-RTOS-DARK-GREEN
│   │   │                       └─ SYN-RTOS-DARK-RED
│   │   │
│   │   └─ usr/local/bin/
│   │       └─ choose-mirror              # Tool used in the ISO runtime
│   │
│   ├─ packages.x86_64                    # Packages in the LIVE ISO
│   ├─ bootstrap_packages.x86_64          # Early bootstrap packages
│   ├─ efiboot/                           # UEFI bootloader (systemd-boot)
│   ├─ grub/                              # GRUB BIOS bootloader
│   ├─ syslinux/                          # Syslinux BIOS bootloader
│   ├─ pacman.conf                        # Pacman config used during ISO build
│   └─ profiledef.sh                      # ArchISO profile definition
│
├─ ISO_OUTPUT/                            # Completed ISOs placed here
│
├─ Graphviz/                              # Visual docs
│   ├─ syn-os.dot
│   └─ SYN-OS.svg
```

---

### What This Shows (the important distinction)

#### **(A) airootfs/**
This is the **ISO’s live system**:  
- `/etc`, `/usr`, `/root`, systemd units  
- installer scripts  
- networking and SSH defaults  
- mirror selection service  
- temporary runtime only

#### **(B) DotfileOverlay/**
This is the **installed system’s defaults**:  
- `/etc/skel/.config/*`  
- LabWC config  
- Waybar modules + CSS  
- Kitty, Qt5ct, Ranger configs  
- themes added under `/usr/share/themes`  

These are merged by Stage 1 into the target system’s filesystem.


This structure ensures that:
- Every file placed on the final system is visible in the repo  
- No configuration is generated implicitly  
- Overlays remain modular and easy to replace  
- Packages and logic stay separated and readable  

### Summary

SYN‑OS is not a “preconfigured Arch distro.”  
It is a **script‑driven system builder** with a clear directory structure, consistent staging process, and modular overlays. Boot the ISO, run `syntax990`, and the system is built exactly from what you see in this repository — no hidden steps, no opaque tooling, no surprises.
## Building Your Own ISO

SYN‑OS includes everything required to rebuild the entire operating system image from source.  
If you have **Arch Linux**, **Arch‑based distros**, or even **SYN‑OS itself**, you can create a fresh ISO locally.

This is possible because the repository *is* a complete **ArchISO profile** containing:

- package arrays (`syn-packages.zsh`)  
- stage scripts (`syn-stage0.zsh`, `syn-stage1.zsh`)  
- filesystem overlays (`overlays/`)  
- user dotfile templates (`DotfileOverlay/skel/`)  
- boot configuration  
- airootfs defaults  

---

### 1. Install Prerequisites

On Arch or any derivative:

```zsh
sudo pacman -S archiso git
```

---

### 2. Clone the SYN‑OS Repository

```zsh
git clone https://github.com/syn990/SYN-OS.git
cd SYN-OS
```

This directory contains the full ISO profile used to generate official releases.

---

### 3. (Optional) Make Changes Before Building

You can adjust anything inside the repo to make the ISO “your own OS”:

**Modify packages:**

```
syn-packages.zsh
```

**Edit installer behaviour:**

```
scripts/syn-stage0.zsh
scripts/syn-stage1.zsh
```

**Change default configs / dotfiles / desktop layout:**

```
overlays/DotfileOverlay/skel/.config/*       (LabWC, Waybar, Kitty, Qt5ct, etc.)
overlays/etc/*                               (system configs)
syn-resources/*                              (menus, scripts, assets)
```

Everything copied into the final system is visible and editable right here.

---

### 4. Build the ISO

Simply run:

```zsh
sudo zsh ./BUILD-SYNOS-ISO.zsh
```

The script will:

1. Validate prerequisites  
2. Generate an ArchISO working directory  
3. Copy the profile structure into `work/`  
4. Build a full **airootfs** from the package arrays  
5. Inject overlays and dotfiles  
6. Apply bootloader configuration  
7. Produce a complete ISO under:

```
out/
 └── syn-os-<date>.iso
```

This ISO is functionally identical to official releases.

---

### 5. Use Your Custom ISO

- Flash it to USB (dd / Rufus / Ventoy / BalenaEtcher)  
- Boot into the live environment  
- Run the installer:

```zsh
syntax990
```

The system you install will reflect **all modifications you made** in the repo —  
packages, defaults, dotfiles, LabWC layout, Waybar configuration, everything.

---

### Why This Matters

Many users eventually want to:
- rebuild SYN‑OS with different packages  
- change LabWC or Waybar defaults  
- remove certain services  
- replace themes or fonts  
- add custom scripts  
- use SYN‑OS as a base for their own distro / spin  

This build system is intentionally simple, modular, and transparent so anyone can remaster it with minimal effort and full visibility.

If you can edit a directory, you can change the OS.
## License

SYN-OS is licensed under the **MIT License**. See the [LICENSE](LICENSE) file for more details.

![MIT License](./Images/LICENSE.png)

## Project History and Installer Evolution

SYN‑OS began as an experimental bootstrap project called **SYN‑RTOS**, built around a pair of simple shell scripts that installed Arch Linux with custom packages and dotfiles. Over time, the system matured into a modular, structured installer with staged execution, overlay directories, and a defined ArchISO profile. Each release refined the build process, dotfile strategy, and installation logic.

### Release Timeline

| Release | Date / Version | Key Focus |
|--------|-----------------|-----------|
| **SYN‑RTOS (V1–V3)** | 2018–2022 | Prototype two‑script installer; manual partition definitions; long monolithic package list; root overlay copied by hand. |
| **SYN‑OS V4** | 2023–2024 | First modular installer layout; separated shell components; clearer directory structure; early `syn-stage0.sh`. |
| **Chronomorph** | Feb 2024 | First named release; refined V4 installer; lightweight GUI using Openbox + Tint2. |
| **Soam‑Do‑Huawei** | May 2024 | Incremental script improvements; adjusted package selection. |
| **VOLITION** | Jun 2024 | Further refinement of scripts, theming, and overall stability. |
| **ArchTech Corp. Edition** | Jul 2024 | Corporate-oriented customisations; issues with pacman integration and dotfile drift identified. |
| **M‑141** | Nov 2024 | Pre‑canonical release; improved documentation and general polish. |
| **SYNTEX** | Apr 2025 | Removal of AI‑generated code; return to minimal, intentional installer logic. |
| **XENITH** | Jan 2026 | Start of the transition away from X11; Openbox + Polybar deprecated. Experimental Wayland groundwork. |
| **SYNAPTICS** | Feb 2026 | Comprehensive overhaul; full Wayland integration; LabWC default session; legacy Openbox themes maintained for compatibility. |

---

### Installer Evolution

The **`syn-stage0`** script is the core of the installation pipeline. Its evolution reflects the shift from experimental scripts to a maintainable, transparent, and predictable system builder.

| Aspect | Early `syn-stage0.sh` | Modern `syn-stage0.zsh` |
|--------|------------------------|--------------------------|
| **Interpreter** | `/bin/sh` | `/bin/zsh` for better syntax and features |
| **Structure** | Single linear script using global variables | Modular functions (`syn_os_environment_prep`, `disk_processing`, `pacstrap_sync`, etc.) |
| **Boot Mode Handling** | Hard‑coded for UEFI/GPT only | Automatically detects UEFI vs MBR and branches safely |
| **Package Handling** | One long string (`SYNSTALL`) | Arrays grouped by purpose (core, services, environment, user apps, dev tools, fonts, optional) |
| **User Prompts** | Basic `read -p` confirmations | Controlled confirmation logic, colourised warnings, structured messaging |
| **Dotfiles & Scripts** | Manual copy of `root_overlay` and separate stage scripts | Uses `DotfileOverlay/` directory; unified stage scripts packaged inside airootfs |
| **Error Handling** | Minimal checks; script terminated on failure | Centralised `check_success` after each critical action |
| **Motivation for Change** | Rapid prototype to create a customised Arch install | Maintainability, safety, clarity; easy replacement or extension of components |

### Rationale

Breaking the process into functions made it easier to test and modify one piece without disturbing the rest.  Detecting the boot environment (UEFI vs MBR) removed hard‑coded assumptions that could brick a system.  Grouping packages into arrays lets users add or remove categories (developer tools, optional extras) with a single edit instead of parsing a monolithic string.  Finally, copying dotfiles and scripts from clearly named overlay directories encourages users to personalise the system without digging through obscure paths.

---
[Click to view vector map of SYN-OS structure](https://raw.githubusercontent.com/syn990/SYN-OS/078920fac9381bd52b37b4c975daf4ddea8b4cc2/SYN-OS/Graphviz/SYN-OS-wayland.svg)  

![GRAPHVIZ STRUCTURE](./SYN-OS/Graphviz/SYN-OS-wayland.svg)
