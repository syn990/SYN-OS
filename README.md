# SYN-OS: A Customizable Arch Linux-based Operating System

## Introduction
Welcome to SYN-OS, a highly customizable operating system developed by William Hayward-Holland (Syntax990). Rooted in the foundations of Arch Linux, SYN-OS aims to provide an efficient and tailor-made computing experience to advanced users. This repository houses the project files and serves as the definitive source of all relevant instructions, resources, and updates about the system and its development.

## Installation Process

### `syn-stage0.sh`
This script kickstarts the installation process and handles essential system configurations.

#### Disk Partitioning
Modify variables related to disk partitioning, including disk wiping, boot partition creation, root partition setup, mount points, and filesystem types to suit your preferences.

#### Package Installation
In SYN-OS, packages are neatly grouped into variables for a seamless installation process. You can modify these variables for personalized package selection. The script uses the Pacstrap tool for package installation.

#### System Configuration
It covers keyboard layout setup, Network Time Protocol (NTP) configuration, DHCP setup for network connectivity, and mirrorlist optimization using the Reflector tool. Additionally, the script ensures the keyring's security and updates package databases.

#### Root Overlay
Place your custom files and configurations in the `SYN-OS-V4/root_overlay` directory. These will be copied into the root directory during the installation.

### Stage 1 - `syn-stage1.sh`
This script wraps up the installation process within the newly created root directory.

#### System Configuration
The script sets up the username, hostname, locale settings, hardware clock, and mirrorlist.

#### Bootloader Configuration
The script leverages the bootctl tool to configure the bootloader.

#### Post-Installation
Post the execution of `syn-stage1.sh`, it is advisable to reboot the system to incorporate all changes and ensure a stable SYN-OS environment.

### Customization
SYN-OS is designed for advanced users with deep understanding of Linux systems, specifically Arch Linux. It allows users to customize aspects like disk partitioning, package selection, locale settings, and system configurations. Users can directly manipulate the build scripts, giving you the power to shape the distro according to your vision, rather than relying on disk images or cloning technology.

### Warning
Exercise caution while using the scripts and customizing variables. Errors or inappropriate changes could lead to data loss or system instability. This OS is intended for advanced users who are comfortable working in a potentially unstable and undocumented environment if deviating from the initial build scripts or dotfiles provided.

## Conclusion
SYN-OS provides a powerful platform tailored for advanced users, enabling a high degree of customization to align with their specific needs. It serves as an ideal stepping-stone for users transitioning to more intermediate operating systems like Arch Linux, providing a clear vision and roadmap. This approach reduces the need for brute force learning through The Arch Wiki. That being said, The Arch Wiki remains an invaluable resource, offering concise instructions and comprehensive learning materials for the broader Linux ecosystem.


#### Check out SYN-OS in action!
[![SYN-OS: An Overview](http://img.youtube.com/vi/fTbNA8TIzDM/0.jpg)](http://www.youtube.com/watch?v=fTbNA8TIzDM "SYN-OS: An Overview")

# Please Note
SYN-OS is a continuously evolving project with frequent script updates. Due to its dynamic nature, data-loss incidents and stress, version control is not fully maintained. The project's structure, form, and design goals are subject to constant revision, and comprehensive documentation is currently not under any major development. As such, I may not always have the bandwidth to maintain a perfectly planned project at all times.
