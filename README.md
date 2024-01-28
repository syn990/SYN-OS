# SYN-OS

**Author:** William Hayward-Holland (Syntax990)

## Overview

SYN-OS is a custom Arch Linux-based operating system designed by William Hayward-Holland, also known as Syntax990. Tailored for users seeking heightened efficiency, flexibility, and scalability, SYN-OS offers a unique blend of customizability and performance.

## Key Features

- **Efficiency and Flexibility:** SYN-OS leverages Arch Linux to provide users with a customizable system. Similar to the arch-installer, this project empowers users to construct their interface, adapting scripts to specific designs and use-cases.

- **Underlying Technology:** Built upon "archiso," SYN-OS utilizes the tools developers use during ISO production. It incorporates a full graphical environment and extensive documentation, enabling users to tailor the system to their preferences.

- **Stability and Versatility:** As a standalone system, SYN-OS delivers stability for modern use-cases on laptops, servers, or workstations. Resembling vanilla Arch Linux, it ensures freedom from vendor lock-in.

- **User Interface Reinterpretation on Minimalism:** A core focus was to implement a zero-touch configuration of a system without a display manager or complete desktop envrioment, instead relying on Openbox as a simple session for X as well as a config drivel panel (tint2) to operate as the UI. A dynamic menu for browing installed applications (which is part of Openbox) has been adapted for the SYN-OS envrioment 

**Canonical Version:** This version serves as the authoritative build, while earlier versions are retained for historical context. It is currently a blend of ArchISO's default baseline as well as various components new and from V4. 2035 refers to what year this system was released.

**SYN-OS-V4:** This system was aiming to break away from the minimal 2 script method in order to better describe the system programatically as various shell components rather than a liner event-driven process.

## Directory Structure

### SYN-DOTFILES

Custom configurations and customizations in `/SYN-OS-2035/SYN-DOTFILES/.config/` include:

- **autostart:** `lxrandr-autostart.desktop` graphical resolution setting.
- **dconf:** `user` settings for the Dconf database.
- **htop:** `htoprc` for user-defined settings in the htop utility.
- **kitty:** `kitty.conf` for the Kitty terminal emulator.
- **openbox:** Various Openbox files for different window manager configurations.
- **pavucontrol-qt:** `pavucontrol-qt.conf` for configuring the QT-based PulseAudio mixer.
- ... [Other configurations]

### Other Dotfiles

- **.oh-my-zsh:** Customizations for the Oh My Zsh shell framework.
- **.themes:** Custom user themes.
- **.wallpaper:** Custom wallpapers.
- **.xinitrc:** X.Org initialization script.
- **.zshrc:** Zsh shell configurations.

### Auto-Start Properties

- **Desktop/Session Management:** The system starts in a tty and requests username/password.
- **xinitrc + openbox + xcompmgr:** Basic compositing with transparency and shadows run alongside openbox.
- **lightdm or anything?:** No, we use startx to avoid unnecessary resources on graphical login screens.

### SYN-INSTALLER-SCRIPTS

Shell scripts for bootstrapping and configuring the system:

- `motd-primer.sh`: Creates the MOTD from the live installer to the resulting system.
- `motd.sh`: Final Message of the Day script.
- `syn-1_chroot.sh`: Chrooting script for executing the final stages of the installer.
- ... [Other scripts]

### SYN-ISO-PROFILE

Core data structures for generating the bootable ISO:

- `airootfs`: AI root filesystem.
  - `etc`: System-wide configurations.
    - `hostname`, `locale.conf`, `localtime`, ... [Other configurations]

[Note: Due to verbosity considerations, not all files and directories are listed. Please refer to the actual repository for a complete structure.]

### SYN-ROOTOVERLAY

Essential root overlay configurations:

- `boot`: Boot-related configurations.
  - `loader`: Bootloader files.
    - `entries`, `loader.conf`: Bootloader configurations.
- `etc`: System-wide configurations.
  - `issue`, `os-release`: Login issue file and operating system metadata.
  - ... [Other configurations]

### SYN-TOOLS

Variety of utility scripts:

- `equip-profile-with-repo.sh`: Script to equip the profile with the repository.
- `REBUILD_ISO.sh`: Script to rebuild the ISO.

## Quick Start Guide

1. Clone the repository: `git clone https://github.com/syn990/SYN-OS.git`
2. Install requisite dependencies: `sudo pacman -S archiso`
3. Edit `/SYN-OS-2035/SYN-ISO-PROFILE` and its `airootfs`.
4. Run `sudo mkarchiso -v /path/to/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE`
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
```

Feel free to adjust the content as needed.