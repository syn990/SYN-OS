# SYN-OS

Welcome to SYN-OS, an operating system project built with customization and ease of use in mind. This README provides an overview of the project and serves as a guide to get started.
  
  
  
  
  

<p align="center">
  <img src="./Images/Screenshot_20230614_015618.png" alt="SYN-OS Image">
</p>
  
  
  
  
  
  
## Overview

A compact distribution grounded in Arch Linux, meticulously designed by William Hayward-Holland (Syntax990). This operating system promotes a streamlined and highly adaptable desktop environment through the integration of Tint2 and Openbox. Uniquely, SYN-OS excels in optimizing resource utilization and elevating performance, making it an ideal choice for rejuvenating outdated hardware with the ability to run contemporary packages. Furthermore, it is tailored to deliver superior control and performance on advanced systems. All project-related files, resources, updates, and directives are conveniently hosted in a comprehensive, centralized repository.

<p align="center">
  <img src="./Images/SYN-OS.PNG" alt="SYN-OS Image">
</p>

## Getting Started

SYN-OS is a modular operating system that comprises several components and directories. Here's a brief description of each:

- **SYN-DOTFILES**: Contains a collection of dotfiles for customizing various applications and tools. Refer to the README file in this directory for more information on how to use and customize the dotfiles.

- **SYN-INSTALLER-SCRIPTS**: Houses installer scripts that facilitate the setup and configuration of SYN-OS-V4. Details about each script and instructions for running them can be found in the README file inside this directory.

- **SYN-ISO-PROFILE**: Includes the ISO profile for building the operating system image. This directory contains the necessary files and configurations. Refer to the README file within this directory for instructions on modifying or customizing the ISO profile.

- **SYN-ROOTOVERLAY**: Contains the root overlay for the operating system. It includes boot-related files and configuration in the "boot" directory, as well as system-wide configurations in the "etc" directory. See the README file in this directory for more details on how to use the root overlay effectively.

- **SYN-TOOLS**: Provides various tools, scripts, or utilities that can be useful in the context of SYN-OS-V4. Consult the README file inside this directory to learn more about each tool and instructions for using them.

To forge a customized ISO of SYN-OS employing the SYN-ISO-PROFILE, please follow these comprehensive steps:

1. **Obtain the Repository:** Clone or download this repository to your local environment.
```bash
git clone https://github.com/syn990/SYN-OS.git
```
2. **Ensure Necessary Dependencies:** Check if all required dependencies, such as `mkarchiso`, are already installed. If not, install them before moving forward.
```bash
sudo pacman -S archiso
```
3. **Modify the SYN-ISO-PROFILE:** The SYN-ISO-PROFILE serves as the blueprint for your custom ISO. Tailor it to your preferences by choosing your package selection, configuring dot files, and implementing any additional adjustments.
4. **Populate the `airootfs` Directory:** Any files you want to be present on the ISO should be copied into the `airootfs/root` directory. These might be configuration files, scripts, or other assets.
```bash
cp YOUR_FILES SYN-OS/SYN-ISO-PROFILE/airootfs/root/
```
5. **Adjust the Installer Scripts:** Alter the installer scripts by modifying the relevant variables to correspond with your desired installation settings.
6. **Commence the Build Process:** Begin the generation of your personalized ISO by running the build command: `sudo mkarchiso -v path/to/SYN-ISO-PROFILE`.
```bash
sudo mkarchiso -v SYN-OS/SYN-ISO-PROFILE
```
7. **Patience is Key:** Allow the build process to complete. The resulting custom ISO will be located in the `out` directory, ready for you to explore your tailored SYN-OS experience.

```Note: Be aware that the build process can take a significant amount of time, depending on your system's capabilities and the customizations you've implemented.```

## Installation Process Explained...

#### Disk Partitioning
Modify variables related to disk partitioning, including disk wiping, boot partition creation, root partition setup, mount points, and filesystem types to suit your preferences.

#### Package Installation
In SYN-OS, packages are neatly grouped into variables for a seamless installation process. You can modify these variables for personalized package selection. The script uses the Pacstrap tool for package installation.

#### Live System Configuration
It covers keyboard layout setup, Network Time Protocol (NTP) configuration, DHCP setup for network connectivity, and mirrorlist optimization using the Reflector tool. Additionally, the script ensures the keyring's security and updates package databases.

#### Root Overlay + Dotfiles
Place your custom files and configurations in the `SYN-OS-V4/root_overlay` directory. These will be copied into the root directory during the installation. Be advised the SYN-OS dotfiles can be found in /etc/skel. This is to ensure that all users created always get the same constistent configuraiton, as defined from the applications included via the intial pacstrap.

![SYN-OS Image](Images/SYN-ROOTOVERLAY.png)

```Note: When adding packges/configuration changes to SYN-ISO-PROFILE before building always ensure /etc/skel has the accompanying dotfiles.```

#### System Configuration
The installer sets up the username, hostname, locale settings, hardware clock, and mirrorlist.

#### Bootloader Configuration
The installer leverages the bootctl tool to configure the bootloader as a single disk gpt SYN-ROOTOVERLAY/boot contains neccessary information.

### Customization
SYN-OS is designed for advanced users with deep understanding of Linux systems, specifically Arch Linux. It allows users to customize aspects like disk partitioning, package selection, locale settings, and system configurations. Users can directly manipulate the build scripts, giving you the power to shape the distro according to your vision, rather than relying on disk images or cloning technology.

<p align="center">
  <img src="./Images/SYN-TOOLS.png" alt="SYN-OS Image">
</p>
<p align="center">
  <img src="./Images/SYN-INSTALLER-SCRIPTS.png" alt="SYN-OS Image">
</p>

### [SYN-INSTALLER-MAIN.sh](https://github.com/syn990/SYN-OS/blob/main/SYN-OS-V4/SYN-INSTALLER-SCRIPTS/SYN-INSTALLER-MAIN.sh)

This is the main script to execute the installation of SYN-OS, an Arch Linux ISO project. Once the ISO has been created and booted, running this script is the primary step to initialize the system setup.

SYN-INSTALLER-MAIN.sh is the cornerstone of the SYN-OS installation process. It sets up partitions, filesystems, mounting points, tests network connectivity, sets up the keyboard layout and NTP, checks the accessibility of Arch Linux repositories, and wipes disks. It partitions and formats the drive according to whether EFI variables are present or not. Then it manages package installation, sets up the keyring, updates mirror lists, generates fstab, copies root overlay materials, and prepares the system for the next stage of installation.

This script also serves as a gateway to the remainder of the installation process by sourcing several other scripts in the `SYN-INSTALLER-SCRIPTS` directory. Each of these sourced scripts performs specific tasks, and their code can be reviewed individually for a deeper understanding of the installation process.

### [syn-disk-variables.sh](https://github.com/syn990/SYN-OS/blob/main/SYN-OS-V4/SYN-INSTALLER-SCRIPTS/syn-disk-variables.sh)

This script is where you define the partitioning scheme for the SYN-OS installation. It includes variables that determine which devices will be targeted for formatting, partitioning, and mounting.

Modify these variables to match your [specific configuration](https://man.archlinux.org/man/lsblk.8.en#:~:text=lsblk%20lists%20information%20about%20all,types%20from%20the%20block%20device.). This is where the script decides on which disk to destroy and format.:

Disk to be wiped: `WIPE_DISK_990="/dev/vda"`  
Boot Partition: `BOOT_PART_990="/dev/vda1"`  
Root Partition: `ROOT_PART_990="/dev/vda2"`  
Location to the new system's boot directory: `BOOT_MOUNT_LOCATION_990="/mnt/boot"`  
Location to the new system's root directory: `ROOT_MOUNT_LOCATION_990="/mnt/"`  
Filesystem for the boot partition: `BOOT_FILESYSTEM_990="fat32"`  
Filesystem for the root partition: `ROOT_FILESYSTEM_990="f2fs"`

### [syn-pacstrap-variables.sh](https://github.com/syn990/SYN-OS/blob/main/SYN-OS-V4/SYN-INSTALLER-SCRIPTS/syn-pacstrap-variables.sh)

This script is responsible for defining the package installation variables used by pacstrap during the initial setup of the main system.

The script includes multiple variables that contain the names of packages to be installed. These variables can be modified to add or remove packages as needed to customize the SYN-OS installation.

Here are the main package variables: (All packages are using Arch Linux offical repositories)
- `basePackages`: Basic system packages required for the initial setup.
- `systemPackages`: Packages for audio, networking, and other system-related utilities.
- `controlPackages`: Packages for controlling the system settings.
- `wmPackages`: Packages for window managers and Xorg server setup.
- `cliPackages`: Command-line interface (CLI) utilities.
- `guiPackages`: Graphical user interface (GUI) utilities.
- `fontPackages`: Packages for fonts and font rendering.
- `cliExtraPackages`: Additional CLI utilities for specialized tasks.
- `guiExtraPackages`: Additional GUI utilities for specific applications.
- `vmExtraPackages`: Packages for virtualization (commented out).

The `SYNSTALL` variable combines all the package variables to form the pacstrap command. When executed with the specified mount point, this command installs all the packages listed in the `SYNSTALL` variable to the target system.

You can directly execute the pacstrap command with the `$SYNSTALL` variable at the end of the `SYN-INSTALLER-MAIN.sh` script to install the defined packages. Alternatively, you have the flexibility to customize the installation process by modifying the package variables or adding an additional pacstrap command as needed.

Please ensure that the package names are valid and accessible through the configured mirrors.

Refer to the `syn-pacstrap-variables.sh` script in the [SYN-INSTALLER-SCRIPTS](https://github.com/syn990/SYN-OS/tree/main/SYN-OS-V4/SYN-INSTALLER-SCRIPTS) directory for more details.

