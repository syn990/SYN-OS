# SYN-OS

**Author:** William Hayward-Holland (Syntax990)

## Overview

SYN-OS is an advanced Arch Linux-based operating system, meticulously crafted by William Hayward-Holland, also known as Syntax990. Tailored for individuals with heightened demands for efficiency, flexibility, and scalability, SYN-OS stands out as a project aiming to provide a unique blend of customizability and performance.

<iframe width="560" height="315" src="https://www.youtube.com/embed/QWRoTm8-x84" frameborder="0" allowfullscreen></iframe>

<iframe width="560" height="315" src="https://www.youtube.com/embed/2aPcttW8LJk" frameborder="0" allowfullscreen></iframe>

<iframe width="560" height="315" src="https://www.youtube.com/embed/RZXVWXwVmeg" frameborder="0" allowfullscreen></iframe>

## Key Features

- **Efficiency and Flexibility:** SYN-OS leverages Arch Linux to offer users a quick template for deploying a custom-curated system. Much like the arch-installer, this project provides a comprehensive overview, empowering users to construct the interface and understand the minimal system, adapting scripts to their specific designs and use-cases.

- **Underlying Technology:** The operating system is an illusion crafted from the usage of "archiso," utilizing the same tools employed by developers during the release engineering ISO production. SYN-OS is an adapted version of this project, incorporating a full graphical environment and a suite of software. Its documentation allows users to seamlessly shift the system according to their preferences, ensuring a personalized experience.

- **Stability and Versatility:** SYN-OS, when used standalone, delivers a stable and simple operating system capable of addressing the most modern use-cases for laptops, servers, or workstations. Resembling vanilla Arch Linux, SYN-OS guarantees freedom from vendor lock-in. 

**Canonical Version:** SYN-OS-2035 serves as the authoritative build, while earlier versions are retained for historical context.

#### Directories and Files in `/SYN-OS-2035/SYN-DOTFILES/.config/`

Dotfiles are the personalized configurations and customizations that elevate SYN-OS to meet user preferences. The `/SYN-OS-2035/SYN-DOTFILES/.config/` directory houses various configurations:

- **autostart**: Stores the `lxrandr-autostart.desktop` graphical resolution setting.
- **dconf**: Contains `user` settings for the Dconf database, a low-level configuration system.
- **htop**: Houses `htoprc`, which holds user-defined settings for the htop utility.
- **kitty**: Contains `kitty.conf`, a configuration file for the Kitty terminal emulator.
- **openbox**: Holds multiple Openbox files including `autostart`, `environment`, `menu.xml`, and `rc.xml` for various Openbox window manager configurations.
- **pavucontrol-qt**: Includes `pavucontrol-qt.conf` for configuring the QT-based PulseAudio mixer.
- **pcmanfm-qt/default**: Contains `recent-files.conf` and `settings.conf` for PcmanFM-Qt, a file manager.
- **pulse**: Manages PulseAudio settings including databases and default sink/source configurations.
- **qt5ct/colors**: Houses `syntax990.conf` for QT-based applications. These are the SYN-OS qt window colours. 
- **ranger**: Consists of multiple configuration files for the Ranger file manager.
- **tint2**: Includes a variety of tint2 configurations, this is the panel. Themes like `SYN-RTOS-DARKRED_TOP.tint2rc` are included.
- **vlc**: Contains `vlc-qt-interface.conf` and `vlcrc` for VLC media player configurations.

#### Other Dotfiles

- **.oh-my-zsh**: Customisations for the Oh My Zsh shell framework.
- **.themes**: Stores custom user themes.
- **.wallpaper**: Holds custom wallpapers.
- **.xinitrc**: X.Org initialisation script.
- **.zshrc**: Zsh shell configurations.

#### Auto-Start Properties

- ** Desktop/Session Managment ? ** - The installed system starts in a tty and requests username/password.
- ** xinitrc + openbox + xcompmgr ** - xcompmgr provides basic compositing such as transparency and shadows and is run along side openbox.
- ** lightdm or anything? ** Nope... We use startx to ensure no bloat or wasted resources on daemons managing a graphical insecure login screen.

#### SYN-INSTALLER-SCRIPTS

Shell scripts for bootstrapping and configuring the system.

- `motd-primer.sh`: Creates the MOTD from the live installer to the resulting system.
- `motd.sh`: Final Message of the Day script.
- `syn-1_chroot.sh`: Chrooting script which executes the final tages of the installer on the live system.
- `syn-ascii-art.sh`: ASCII Art as it's own shell script.
- `syn-disk-variables.sh`: Disk variable definitions where wiping and formatting occurs.
- `syn-installer-functions.sh`: Core installer functions.
- `SYN-INSTALLER-MAIN.sh`: Main installation script. This is the one that is run, all the others are called from it.
- `syn-pacstrap-variables.sh`: Pacstrap utility variables.

#### SYN-ISO-PROFILE

Holds the core data structures required for generating the bootable ISO.

- `airootfs`: AI root filesystem.
  - `etc`: System-wide configurations.
    - `hostname`: System hostname.
    - `locale.conf`: Localisation configurations.
    - `localtime`: Local time settings.
    - ... [Other configurations]

[Note: Due to verbosity considerations, not all files and directories are listed. Please refer to the actual repository for a complete structure.]

#### SYN-ROOTOVERLAY

Contains essential root overlay configurations.

- `boot`: Boot-related configurations.
  - `loader`: Bootloader files.
    - `entries`: Bootloader entries.
    - `loader.conf`: Main loader configuration.

- `etc`: System-wide configurations.
  - `issue`: Login issue file.
  - `os-release`: Operating system metadata.
  - ... [Other configurations]

#### SYN-TOOLS

Holds a variety of utility scripts.

- `equip-profile-with-repo.sh`: Script to equip the profile with the repository.
- `REBUILD_ISO.sh`: Script to rebuild the ISO.

---

## Quick Start Guide

1. Clone the repository: `git clone https://github.com/syn990/SYN-OS.git`
2. Install requisite dependencies: `sudo pacman -S archiso`
3. Edit `/SYN-OS-2035/SYN-ISO-PROFILE` and its `airootfs`.
4. `sudo mkarchiso -v /home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE`
5. Output will be in 'out' directory.
6. Boot that ISO up.

## License

Licensed under MIT. See [LICENSE](https://github.com/syn990/SYN-OS/blob/main/LICENSE) for details.

<p align="center">
  <img src="./Images/LICENSE.png" alt="SYN-OS License">
</p>

## Support

Contact via [LinkedIn](https://www.linkedin.com/in/william-hayward-holland-990/) or `william@npc.syntax990.com`.

For further guidance, please refer to [The Arch Wiki](https://wiki.archlinux.org).
