# SYN-OS

##### **Author:** William Hayward-Holland (Syntax990)

### Overview

SYN-OS is an ArchISO project, functioning as a custom Arch Linux-based operating system. 

<p align="center">
  <img src="SYN-OS-V3 or Earlier/repo/images/readme.md/FIRST-IMAGE.png" alt="SYN-OS License">
</p>

Developed by William Hayward-Holland (Syntax990), SYN-OS utilises build scripts to systematically install and configure an Arch Linux system according to user preferences. Tailored for enhanced efficiency, flexibility, and scalability, SYN-OS offers a unique blend of customizability and performance.

# Key Features

- **Efficiency and Flexibility:** SYN-OS leverages Arch Linux, empowering users to customise their system.
- **Underlying Technology:** Built upon "archiso," SYN-OS utilises tools developers use during ISO production.
- **Stability and Versatility:** As a standalone system, SYN-OS delivers stability for modern use-cases on various platforms.
- **User Interface Reinterpretation on Minimalism:** Focus on zero-touch configuration with Openbox as the primary session for X.

## Openbox

<p align="center">
  <img src="./Images/openbox.png" alt="SYN-OS License">
</p>

## Openbox + SYN-OS

<p align="center">
  <img src="./Images/openbox-SYNOS.png" alt="SYN-OS License">
</p>

## History

- **SYN-OS:** Baseline version not ready for use.
- **SYN-OS-2035:** A mixed build with a blend of ArchISO's default baseline and components from V4. (Failed)
- **SYN-OS-V4:** Aiming to break away from the minimal 2-script method to describe the system programmatically. (Messy / Cancelled)
- **Earlier SYN-RTOS:** Quick, dirty, and functional, but hard to interpret and lacks error control. (Historic, Very Useful)

### The Dotfiles

In UNIX-like operating systems, configuration files often start with a dot ('.') and are commonly referred to as "dotfiles." These files store user-specific settings and preferences for various applications, ensuring a personalized computing experience. SYN-OS embraces the concept of dotfiles to empower users with a customizable environment.

Within SYN-OS, dotfiles are a crucial part of the system installer. They contain configurations for applications, window managers, and other components that contribute to the overall look and feel of the system. During the installation process, these dotfiles are copied into the ISO and later integrated into user profiles to ensure a consistent and tailored experience on SYN-OS.

Let's explore some key dotfiles included in SYN-OS:

- **autostart:** `lxrandr-autostart.desktop` (Graphical resolution for LXrandr).
- **dconf:** `user` (User settings for Dconf).
- **htop:** `htoprc` (User-defined settings for htop).
- **kitty:** `kitty.conf` (Configuration for Kitty terminal emulator).
- **openbox:** (Various Openbox files for window manager configurations).
- **pavucontrol-qt:** `pavucontrol-qt.conf` (Configuration for QT-based PulseAudio mixer).
- **.oh-my-zsh:** (Customizations for Oh My Zsh).
- **.themes:** (Custom user themes).
- **.wallpaper:** (Custom wallpapers).
- **.xinitrc:** (X.Org initialization script).
- **.zshrc:** (Zsh shell configurations).


These dotfiles collectively contribute to the individuality and functionality of SYN-OS, allowing users to shape their computing environment according to their preferences.


### Auto-Start Properties

- **Embracing "Terminal By Design":** The system starts in a tty, prompting for a username/password, and optionally employs an X session through `xinitrc`, invoked by executing `startx`.
- **Why no lightdm or session management?:** We choose startx to minimize resource usage, steering clear of unnecessary overhead from graphical login screens.
- **`xinitrc` + `openbox` + `xcompmgr`:** This combination involves basic compositing with transparency and shadows, operating alongside openbox.


### Installer Scripts

Shell scripts for bootstrapping and configuring the system:

- `syn-stage0.sh` : Defines the disks, installs the packages and gets the system ready for chroot (to finalise)
- `syn-stage1.sh`: Chrooting script for executing the final stages of the installer.
- `motd-primer.sh`: Creates the MOTD from the live installer to the resulting system., combined with `motd.sh` which is somewhat working in 2035.
- `SYN-INSTALLER-MAIN.sh` : An attempt at being the main thread of the other shell scripts, being the main script for installing SYN-OS.
- `syn-disk-variables.sh` : Self-contained functions for pacstrap when setting up the system from the live ISO
- `syn-ascii-art.sh` : Functions that contain ASCII art used for printf and echo.
- `syn-pacstrap-variables.sh` : Self-contained functions for pacstrap when setting up the system from the live ISO.
- `syn-installer-functions.sh` : Contains various extra non-essential components.


### Archiso Profile

Core data structures for generating the bootable ISO:

SYN-OS is built from the baseline version of ArchISO, a core set of minimal features to boot the kernel.

- `airootfs`: AI root filesystem.
  - `etc`: System-wide configurations.
    - `hostname`, `locale.conf`, `localtime`, ... [Other configurations]

[Note: Due to verbosity considerations, not all files and directories are listed. This all needs to be properly defined as it consists of a loose collection of text files.]

### SYN-ROOTOVERLAY

Data is to be copied into the archiso, as it is supposed to act as a pre-configuration ISO. This needs removing and the documentation needs improvement as this is complicating the setup and reducing reliability.

Essential root overlay configurations include:

- `boot`: Boot-related configurations.
  - `loader`: Bootloader files.
    - `entries`, `loader.conf`: Bootloader configurations.
- `etc`: System-wide configurations.
  - `issue`, `os-release`: Login issue file and operating system metadata.
  - ... [Other configurations]

### External OS Tools

Variety of utility scripts:

- `equip-profile-with-repo.sh`: Script to equip the profile with the repository.
- `SYN-BUILDER.ZSH`: Script to rebuild the ISO.
- `Graph.sh`: Dynamic script to scan pwd, draw a directory tree in a dot matrix, then produce a few PDF and various algorithms to display the data.

## Quick Start Guide

1. Clone the repository: `git clone https://github.com/syn990/SYN-OS.git`
2. Install requisite dependencies: `sudo pacman -S archiso`
3. Edit `/SYN-OS/SYN-ISO-PROFILE` and the contents of `airootfs` as well as the packages file to modify what is pre-included in the ISO.
4. Run `SYN-BUILDER.ZSH`
5. Find the output in the 'out' directory.
6. Boot the ISO.

## License

Licensed under MIT. See [LICENSE](https://github.com/syn990/SYN-OS/blob/main/LICENSE) for details.

<p align="center">
  <img src="./Images/LICENSE.png" alt="SYN-OS License">
</p>

## Support

Contact via [LinkedIn](https://www.linkedin.com/in/william-hayward-holland-990/) or `william@npc.syntax990.com`.

For further guidance, please refer to [The Arch Wiki](https://wiki.archlinux.org).