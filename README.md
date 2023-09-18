# SYN-OS

SYN-OS is a compact Arch Linux-based distribution created by William Hayward-Holland (Syntax990). It provides a streamlined desktop environment with Tint2 and Openbox, optimized for both older and modern hardware. This distribution is designed for advanced users looking to customize their system.

**Note:** This installer is not for a quick Arch Linux installation; it's intended for customization and learning. It's expected that you are already running the Arch Linux base system and are ready to build your own spin-off...

[Visit the Wiki!](https://github.com/syn990/SYN-OS/wiki)

## Repository Directories

- **SYN-ISO-PROFILE:** Contains the ISO profile for building the OS image.
- **SYN-ROOTOVERLAY:** Contains system-wide configurations.
- **SYN-DOTFILES:** Includes dotfiles for customizing applications.
- **SYN-INSTALLER-SCRIPTS:** Houses installer scripts.
- **SYN-TOOLS:** Provides various tools and utilities.

## Customization

SYN-OS is meant for advanced users who want to customize disk partitioning, package selection, and more. You can modify scripts and variables as needed.

## Getting Started

1. Clone the repository: `git clone https://github.com/syn990/SYN-OS.git`
2. Install dependencies: `sudo pacman -S archiso`
3. Modify SYN-ISO-PROFILE, populate airootfs, and adjust installer scripts.
4. Build your customized ISO: `sudo mkarchiso -v SYN-OS/SYN-ISO-PROFILE`
5. Be patient; the custom ISO will be in the 'out' directory.

## License

SYN-OS is under the MIT License, allowing you to use, modify, and distribute it for any purpose, including commercial. See the [LICENSE](https://github.com/syn990/SYN-OS/blob/main/LICENSE) for details.

<p align="center">
  <img src="./Images/LICENSE.png" alt="SYN-OS Image">
</p>

## Support

For support or questions, you can contact William Hayward-Holland via [LinkedIn](https://www.linkedin.com/in/william-hayward-holland-990/) or email at william@npc.syntax990.com.

Please refer to [The Arch Wiki](https://wiki.archlinux.org) for resources beyond the project's scope.
