# SYN-OS: Customizable Arch Linux-based Operating System

#Be Advised:
THIS IS A LOOSE COLLECTION OF CONSTANTLY UPDATED SCRIPTS WITH MISSING VERSION CONTROL
THE STRUCTURE, FORM AND DESIGN GOALS ARE CONSTANTLY CHANGING AND DOCUMENTATION HAS YET TO EXIST
I SIMPLY DO NOT HAVE TIME TO MAINTAIN A PLANNED PROJECT

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

##### CLICK TO VIEW VIDEO
[![SYN-OS: An Overview](http://img.youtube.com/vi/fTbNA8TIzDM/0.jpg)](http://www.youtube.com/watch?v=fTbNA8TIzDM "SYN-OS: An Overview")


# Custom Operating System Profile├── SYN-OS-ARCHISO_PROFILE
├── SYN-OS-V3 or Earlier
│   ├── README.md
│   ├── repo
│   │   └── images
│   │       └── readme.md
│   │           └── FIRST-IMAGE.png
│   ├── SYN-OS-ARCHISO_PROFILE
│   │   ├── airootfs
│   │   │   ├── etc
│   │   │   │   ├── locale.conf
│   │   │   │   ├── mkinitcpio.conf
│   │   │   │   ├── mkinitcpio.d
│   │   │   │   │   └── linux.preset
│   │   │   │   ├── shadow
│   │   │   │   ├── ssh
│   │   │   │   │   └── sshd_config
│   │   │   │   └── systemd
│   │   │   │       ├── network
│   │   │   │       │   └── 20-ethernet.network
│   │   │   │       ├── system
│   │   │   │       │   ├── cloud-init.target.wants
│   │   │   │       │   │   ├── cloud-config.service -> /usr/lib/systemd/system/cloud-config.service
│   │   │   │       │   │   ├── cloud-final.service -> /usr/lib/systemd/system/cloud-final.service
│   │   │   │       │   │   ├── cloud-init-local.service -> /usr/lib/systemd/system/cloud-init-local.service
│   │   │   │       │   │   └── cloud-init.service -> /usr/lib/systemd/system/cloud-init.service
│   │   │   │       │   ├── multi-user.target.wants
│   │   │   │       │   │   ├── hv_fcopy_daemon.service -> /usr/lib/systemd/system/hv_fcopy_daemon.service
│   │   │   │       │   │   ├── hv_kvp_daemon.service -> /usr/lib/systemd/system/hv_kvp_daemon.service
│   │   │   │       │   │   ├── hv_vss_daemon.service -> /usr/lib/systemd/system/hv_vss_daemon.service
│   │   │   │       │   │   ├── sshd.service -> /usr/lib/systemd/system/sshd.service
│   │   │   │       │   │   ├── systemd-networkd.service -> /usr/lib/systemd/system/systemd-networkd.service
│   │   │   │       │   │   ├── systemd-resolved.service -> /usr/lib/systemd/system/systemd-resolved.service
│   │   │   │       │   │   ├── vboxservice.service -> /usr/lib/systemd/system/vboxservice.service
│   │   │   │       │   │   ├── vmtoolsd.service -> /usr/lib/systemd/system/vmtoolsd.service
│   │   │   │       │   │   └── vmware-vmblock-fuse.service -> /usr/lib/systemd/system/vmware-vmblock-fuse.service
│   │   │   │       │   ├── network-online.target.wants
│   │   │   │       │   │   └── systemd-networkd-wait-online.service -> /usr/lib/systemd/system/systemd-networkd-wait-online.service
│   │   │   │       │   ├── sockets.target.wants
│   │   │   │       │   │   └── systemd-networkd.socket -> /usr/lib/systemd/system/systemd-networkd.socket
│   │   │   │       │   └── systemd-networkd-wait-online.service.d
│   │   │   │       │       └── wait-for-only-one-interface.conf
│   │   │   │       └── system-generators
│   │   │   │           └── systemd-gpt-auto-generator -> /dev/null
│   │   │   └── root
│   │   ├── bootstrap_packages.x86_64
│   │   ├── efiboot
│   │   │   └── loader
│   │   │       ├── entries
│   │   │       │   └── 01-archiso-x86_64-linux.conf
│   │   │       └── loader.conf
│   │   ├── grub
│   │   │   └── grub.cfg
│   │   ├── packages.x86_64
│   │   ├── pacman.conf
│   │   ├── profiledef.sh
│   │   └── syslinux
│   │       ├── syslinux.cfg
│   │       └── syslinux-linux.cfg
│   ├── SYN-RTOS-OLD
│   │   ├── airootfs
│   │   │   ├── etc
│   │   │   │   ├── hostapd
│   │   │   │   │   └── hostapd.conf
│   │   │   │   ├── hostname
│   │   │   │   ├── hosts
│   │   │   │   ├── initcpio
│   │   │   │   │   └── hooks
│   │   │   │   │       └── archiso
│   │   │   │   ├── locale.conf
│   │   │   │   ├── localtime
│   │   │   │   ├── machine-id
│   │   │   │   ├── mkinitcpio.conf
│   │   │   │   ├── mkinitcpio.d
│   │   │   │   │   └── linux.preset
│   │   │   │   ├── modprobe.d
│   │   │   │   │   ├── broadcom-wl.conf
│   │   │   │   │   └── nvidia-drm.conf
│   │   │   │   ├── motd
│   │   │   │   ├── passwd
│   │   │   │   ├── shadow
│   │   │   │   ├── skel
│   │   │   │   ├── ssh
│   │   │   │   │   └── sshd_config
│   │   │   │   ├── systemd
│   │   │   │   │   ├── journald.conf.d
│   │   │   │   │   │   └── volatile-storage.conf
│   │   │   │   │   ├── logind.conf.d
│   │   │   │   │   │   └── do-not-suspend.conf
│   │   │   │   │   ├── network
│   │   │   │   │   │   ├── 20-ethernet.network
│   │   │   │   │   │   ├── 20-wlan.network
│   │   │   │   │   │   └── 20-wwan.network
│   │   │   │   │   └── system
│   │   │   │   │       ├── choose-mirror.service
│   │   │   │   │       ├── dbus-org.freedesktop.ModemManager1.service
│   │   │   │   │       ├── dbus-org.freedesktop.network1.service
│   │   │   │   │       ├── dbus-org.freedesktop.resolve1.service
│   │   │   │   │       ├── etc-pacman.d-gnupg.mount
│   │   │   │   │       ├── getty@tty1.service.d
│   │   │   │   │       │   └── autologin.conf
│   │   │   │   │       ├── livecd-alsa-unmuter.service
│   │   │   │   │       ├── livecd-talk.service
│   │   │   │   │       ├── multi-user.target.wants
│   │   │   │   │       │   ├── choose-mirror.service
│   │   │   │   │       │   ├── hv_fcopy_daemon.service
│   │   │   │   │       │   ├── hv_kvp_daemon.service
│   │   │   │   │       │   ├── hv_vss_daemon.service
│   │   │   │   │       │   ├── iwd.service
│   │   │   │   │       │   ├── ModemManager.service
│   │   │   │   │       │   ├── pacman-init.service
│   │   │   │   │       │   ├── qemu-guest-agent.service
│   │   │   │   │       │   ├── reflector.service
│   │   │   │   │       │   ├── systemd-networkd.service
│   │   │   │   │       │   ├── systemd-resolved.service
│   │   │   │   │       │   ├── vboxservice.service
│   │   │   │   │       │   ├── vmtoolsd.service
│   │   │   │   │       │   └── vmware-vmblock-fuse.service
│   │   │   │   │       ├── network-online.target.wants
│   │   │   │   │       │   └── systemd-networkd-wait-online.service
│   │   │   │   │       ├── pacman-init.service
│   │   │   │   │       ├── reflector.service.d
│   │   │   │   │       │   └── archiso.conf
│   │   │   │   │       ├── sockets.target.wants
│   │   │   │   │       │   └── systemd-networkd.socket
│   │   │   │   │       ├── sound.target.wants
│   │   │   │   │       │   └── livecd-alsa-unmuter.service
│   │   │   │   │       └── systemd-networkd-wait-online.service.d
│   │   │   │   │           └── wait-for-only-one-interface.conf
│   │   │   │   ├── vconsole.conf
│   │   │   │   └── xdg
│   │   │   │       └── reflector
│   │   │   │           └── reflector.conf
│   │   │   ├── root
│   │   │   │   ├── syn-install.sh
│   │   │   │   ├── SYN-RTOS-V3
│   │   │   │   │   ├── 0.Legacy V1_V2 Files
│   │   │   │   │   │   ├── default-dotfiles
│   │   │   │   │   │   ├── DYNAMIC-OPENBOX-BAR.txt
│   │   │   │   │   │   ├── network_scripts
│   │   │   │   │   │   │   ├── eno1-wlan0_bridge.sh
│   │   │   │   │   │   │   ├── eno1-wlan0_bridge-withoutDHCP.sh
│   │   │   │   │   │   │   └── ip-configure.sh
│   │   │   │   │   │   ├── Raw_Package_list.txt
│   │   │   │   │   │   ├── README.md
│   │   │   │   │   │   ├── syn-installer0.sh
│   │   │   │   │   │   ├── syn-installer1.sh
│   │   │   │   │   │   ├── syn-installerMERGER.sh
│   │   │   │   │   │   └── xibo-related
│   │   │   │   │   │       ├── chromium_fullscreen_kiosk.sh
│   │   │   │   │   │       └── xibo-build
│   │   │   │   │   │           ├── airootfs
│   │   │   │   │   │           │   ├── etc
│   │   │   │   │   │           │   │   ├── hostname
│   │   │   │   │   │           │   │   ├── locale.conf
│   │   │   │   │   │           │   │   ├── mkinitcpio.conf
│   │   │   │   │   │           │   │   ├── mkinitcpio.d
│   │   │   │   │   │           │   │   │   └── linux.preset
│   │   │   │   │   │           │   │   ├── modprobe.d
│   │   │   │   │   │           │   │   │   └── broadcom-wl.conf
│   │   │   │   │   │           │   │   ├── motd
│   │   │   │   │   │           │   │   ├── passwd
│   │   │   │   │   │           │   │   ├── shadow
│   │   │   │   │   │           │   │   ├── ssh
│   │   │   │   │   │           │   │   │   └── sshd_config
│   │   │   │   │   │           │   │   ├── systemd
│   │   │   │   │   │           │   │   │   ├── journald.conf.d
│   │   │   │   │   │           │   │   │   │   └── volatile-storage.conf
│   │   │   │   │   │           │   │   │   ├── logind.conf.d
│   │   │   │   │   │           │   │   │   │   └── do-not-suspend.conf
│   │   │   │   │   │           │   │   │   ├── network
│   │   │   │   │   │           │   │   │   │   ├── 20-ethernet.network
│   │   │   │   │   │           │   │   │   │   └── 20-wireless.network
│   │   │   │   │   │           │   │   │   └── system
│   │   │   │   │   │           │   │   │       ├── choose-mirror.service
│   │   │   │   │   │           │   │   │       ├── etc-pacman.d-gnupg.mount
│   │   │   │   │   │           │   │   │       ├── getty@tty1.service.d
│   │   │   │   │   │           │   │   │       │   └── autologin.conf
│   │   │   │   │   │           │   │   │       ├── livecd-alsa-unmuter.service
│   │   │   │   │   │           │   │   │       ├── livecd-talk.service
│   │   │   │   │   │           │   │   │       ├── pacman-init.service
│   │   │   │   │   │           │   │   │       ├── reflector.service.d
│   │   │   │   │   │           │   │   │       │   └── archiso.conf
│   │   │   │   │   │           │   │   │       └── systemd-networkd-wait-online.service.d
│   │   │   │   │   │           │   │   │           └── wait-for-only-one-interface.conf
│   │   │   │   │   │           │   │   └── xdg
│   │   │   │   │   │           │   │       └── reflector
│   │   │   │   │   │           │   │           └── reflector.conf
│   │   │   │   │   │           │   ├── root
│   │   │   │   │   │           │   │   ├── customize_airootfs.sh
│   │   │   │   │   │           │   │   └── SYNSTALL
│   │   │   │   │   │           │   │       ├── root-path
│   │   │   │   │   │           │   │       │   ├── boot
│   │   │   │   │   │           │   │       │   │   └── loader
│   │   │   │   │   │           │   │       │   │       ├── entries
│   │   │   │   │   │           │   │       │   │       │   └── arch.conf
│   │   │   │   │   │           │   │       │   │       └── loader.conf
│   │   │   │   │   │           │   │       │   └── etc
│   │   │   │   │   │           │   │       │       ├── systemd
│   │   │   │   │   │           │   │       │       │   └── logind.conf
│   │   │   │   │   │           │   │       │       └── X11
│   │   │   │   │   │           │   │       │           └── xorg.conf.d
│   │   │   │   │   │           │   │       │               └── 10-monitor.conf
│   │   │   │   │   │           │   │       └── scripts
│   │   │   │   │   │           │   │           ├── eno1-wlan0_bridge.sh
│   │   │   │   │   │           │   │           ├── ip-configure.sh
│   │   │   │   │   │           │   │           ├── syn-installer0.sh
│   │   │   │   │   │           │   │           └── syn-installer1.sh
│   │   │   │   │   │           │   └── usr
│   │   │   │   │   │           │       └── local
│   │   │   │   │   │           │           ├── bin
│   │   │   │   │   │           │           │   ├── choose-mirror
│   │   │   │   │   │           │           │   ├── Installation_guide
│   │   │   │   │   │           │           │   └── livecd-sound
│   │   │   │   │   │           │           └── share
│   │   │   │   │   │           │               └── livecd-sound
│   │   │   │   │   │           │                   └── asound.conf.in
│   │   │   │   │   │           ├── efiboot
│   │   │   │   │   │           │   └── loader
│   │   │   │   │   │           │       ├── entries
│   │   │   │   │   │           │       │   ├── archiso-x86_64-linux.conf
│   │   │   │   │   │           │       │   └── archiso-x86_64-speech-linux.conf
│   │   │   │   │   │           │       └── loader.conf
│   │   │   │   │   │           ├── packages.x86_64
│   │   │   │   │   │           ├── pacman.conf
│   │   │   │   │   │           ├── profiledef.sh
│   │   │   │   │   │           └── syslinux
│   │   │   │   │   │               ├── archiso_head.cfg
│   │   │   │   │   │               ├── archiso_pxe.cfg
│   │   │   │   │   │               ├── archiso_pxe-linux.cfg
│   │   │   │   │   │               ├── archiso_sys.cfg
│   │   │   │   │   │               ├── archiso_sys-linux.cfg
│   │   │   │   │   │               ├── archiso_tail.cfg
│   │   │   │   │   │               ├── splash.png
│   │   │   │   │   │               └── syslinux.cfg
│   │   │   │   │   └── 1.root_filesystem_overlay
│   │   │   │   │       ├── boot
│   │   │   │   │       │   └── loader
│   │   │   │   │       │       ├── entries
│   │   │   │   │       │       │   └── syn.conf
│   │   │   │   │       │       └── loader.conf
│   │   │   │   │       └── etc
│   │   │   │   │           ├── issue
│   │   │   │   │           ├── os-release
│   │   │   │   │           ├── pacman.conf
│   │   │   │   │           ├── skel
│   │   │   │   │           └── vconsole.conf
│   │   │   │   ├── syn-stage00.sh
│   │   │   │   ├── syn-stage0.sh
│   │   │   │   ├── syn-stage11.sh
│   │   │   │   └── syn-stage1.sh
│   │   │   └── usr
│   │   │       └── local
│   │   │           ├── bin
│   │   │           │   ├── choose-mirror
│   │   │           │   ├── Installation_guide
│   │   │           │   └── livecd-sound
│   │   │           └── share
│   │   │               └── livecd-sound
│   │   │                   └── asound.conf.in
│   │   ├── bootstrap_packages.x86_64
│   │   ├── efiboot
│   │   │   ├── boot
│   │   │   │   ├── refind-dvd.conf
│   │   │   │   └── refind-usb.conf
│   │   │   └── loader
│   │   │       ├── entries
│   │   │       │   ├── archiso_3_ram-x86_64-linux.conf
│   │   │       │   ├── archiso-x86_64-linux.conf
│   │   │       │   └── archiso-x86_64-speech-linux.conf
│   │   │       └── loader.conf
│   │   ├── grub
│   │   │   └── grub.cfg
│   │   ├── lang
│   │   │   ├── cs_CZ
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── de_DE
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── el_GR
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── es_ES
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── fr_FR
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── hu_HU
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── it_IT
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── nl_NL
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── pl_PL
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── pt_PT
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── ro_RO
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── ru_RU
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── sr_RS@latin
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   ├── tr_TR
│   │   │   │   ├── airootfs
│   │   │   │   │   ├── etc
│   │   │   │   │   │   ├── environment
│   │   │   │   │   │   ├── locale.conf
│   │   │   │   │   │   ├── vconsole.conf
│   │   │   │   │   │   └── X11
│   │   │   │   │   │       └── xorg.conf.d
│   │   │   │   │   │           └── 00-keyboard.conf
│   │   │   │   │   └── root
│   │   │   │   │       └── customize_airootfs_lang.sh
│   │   │   │   └── packages.x86_64
│   │   │   └── uk_UA
│   │   │       ├── airootfs
│   │   │       │   ├── etc
│   │   │       │   │   ├── environment
│   │   │       │   │   ├── locale.conf
│   │   │       │   │   ├── vconsole.conf
│   │   │       │   │   └── X11
│   │   │       │   │       └── xorg.conf.d
│   │   │       │   │           └── 00-keyboard.conf
│   │   │       │   └── root
│   │   │       │       └── customize_airootfs_lang.sh
│   │   │       └── packages.x86_64
│   │   ├── packages.x86_64
│   │   ├── pacman.conf
│   │   ├── pacman-testing.conf
│   │   ├── profiledef.sh
│   │   └── syslinux
│   │       ├── archiso_head.cfg
│   │       ├── archiso_pxe.cfg
│   │       ├── archiso_pxe-linux.cfg
│   │       ├── archiso_sys.cfg
│   │       ├── archiso_sys-linux.cfg
│   │       ├── archiso_tail.cfg
│   │       ├── splash.png
│   │       └── syslinux.cfg
│   ├── syn-stage0.sh
│   └── syn-stage1.sh
└── SYN-OS-V4
    ├── root_overlay
    ├── SYN-INSTALLER-SCRIPTS
    │   ├── motd.sh
    │   ├── syn-1_chroot.sh
    │   ├── syn-ascii-art.sh
    │   ├── syn-disk-variables.sh
    │   ├── syn-installer-functions.sh
    │   ├── SYN-INSTALLER-MAIN.sh
    │   └── syn-pacstrap-variables.sh
    └── SYN-TOOLS
        ├── equip-profile-with-repo.sh
        ├── REBUILD_ISO.sh
        └── ShowInterfaceAddrLoop.sh

This repository contains a custom operating system profile that can be used to create a specialized OS image.

## Profile Structure

The profile directory is organized as follows:

