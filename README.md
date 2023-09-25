# SYN-OS

SYN-OS is an advanced Arch Linux-based operating system project, masterminded by William Hayward-Holland (Syntax990). It is tailored for individuals with elevated demands for efficiency, flexibility, and scalability. 

**Canonical Version:** SYN-OS-2035 serves as the authoritative build, while earlier versions remain for historical context.

### SYN-OS Sub-Components

### SYN-DOTFILES

This directory houses personalised configurations and customisations for various tools and utilities employed within the SYN-OS ecosystem. 

#### Directories and Files in `/SYN-OS-2035/SYN-DOTFILES/.config/`

- **autostart**: Stores the `lxrandr-autostart.desktop` graphical resolution setting.
- **dconf**: Contains `user` settings for the Dconf database, a low-level configuration system.
- **htop**: Houses `htoprc`, which holds user-defined settings for the htop utility.
- **kitty**: Contains `kitty.conf`, a configuration file for the Kitty terminal emulator.
- **openbox**: Holds multiple Openbox files including `autostart`, `environment`, `menu.xml`, `menu.xml.1`, and `rc.xml` for various Openbox window manager configurations.
- **pavucontrol-qt**: Includes `pavucontrol-qt.conf` for configuring the QT-based PulseAudio mixer.
- **pcmanfm-qt/default**: Contains `recent-files.conf` and `settings.conf` for PcmanFM-Qt, a file manager.
- **pulse**: Manages PulseAudio settings including databases and default sink/source configurations.
- **qt5ct/colors**: Houses `syntax990.conf` for QT-based applications. 
- **ranger**: Consists of multiple configuration files such as `commands_full.py`, `commands.py`, `rc.conf`, `rifle.conf`, and `scope.sh` for the Ranger file manager.
- **tint2**: Includes a variety of tint2 configurations, from `blank` to various templates like `SYN-RTOS-DARKRED_TOP.tint2rc`.
- **vlc**: Contains `vlc-qt-interface.conf` and `vlcrc` for VLC media player configurations.

#### Other Dotfiles

- **.oh-my-zsh**: Customisations for the Oh My Zsh shell framework.
- **.themes**: Stores custom user themes.
- **.wallpaper**: Holds custom wallpapers.
- **.xinitrc**: X.Org initialisation script.
- **.zshrc**: Zsh shell configurations.

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
