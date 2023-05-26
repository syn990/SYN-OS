# SYN-OS: Customizable Arch Linux-based Operating System

## Overview
SYN-OS is a custom operating system developed by William Hayward-Holland (Syntax990), built on the foundation of Arch Linux. It is designed to provide advanced users with a highly customizable and efficient computing experience.

## Installation Process

### Stage 0 (syn-stage0.sh)
Initiates the installation process, performing critical system configuration tasks.

#### Disk Partitioning
Users can customize disk partitioning by adjusting variables corresponding to disk wiping, boot partition creation, root partition setup, mount points, and filesystem types.

#### Package Installation
SYN-OS categorizes packages into different variables for streamlined installation. Users can modify these variables for personalized package selection. The script employs the Pacstrap tool for package installation.

#### System Configuration
Includes keyboard layout setup, Network Time Protocol (NTP) configuration, DHCP setup for network connectivity, and mirrorlist optimization using the reflector tool. Additionally, the script secures the keyring and updates package databases.

#### Root Overlay
Users can add custom files and configurations to the SYN-OS-V4/root_overlay directory, which are then copied to the root directory during installation.

### Stage 1 (syn-stage1.sh)
Finalizes the installation process within the new root directory.

#### System Configuration
Covers the setup of username, hostname, locale settings, hardware clock, and mirrorlist.

#### Bootloader Configuration
The script employs the bootctl tool to configure the bootloader.

#### Post-Installation
After running syn-stage1.sh, a system reboot is recommended to apply all changes and ensure a stable SYN-OS environment.

## Usage Guidelines

### Expertise
SYN-OS targets advanced users with a comprehensive understanding of Linux systems, particularly Arch Linux.

### Customization
SYN-OS provides extensive customization options, including disk partitioning, package selection, locale settings, and system configurations.

### Caution
Users should exercise caution when using the scripts and customizing variables. Errors or improper modifications could result in data loss or system instability. 

### Documentation
Users are advised to refer to comprehensive documentation and user guides to fully utilize the capabilities of SYN-OS.

## Conclusion
SYN-OS offers a highly customizable and efficient operating system for advanced users, providing the ability to fine-tune the system according to specific requirements.

CLICK TO VIEW VIDEO:
[![SYN-OS: An Overview](http://img.youtube.com/vi/fTbNA8TIzDM/0.jpg)](http://www.youtube.com/watch?v=fTbNA8TIzDM "SYN-OS: An Overview")

