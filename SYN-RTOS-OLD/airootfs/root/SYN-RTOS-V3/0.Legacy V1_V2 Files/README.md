# SYN-RTOS - Syntax990 Real-Time Operating System.
This repo serves as a place to store and manage the source tree, as it's getting quite messy!

Currently there are two main scripts: (these will later be merged into a single script. It can't seem to run arch-chroot without stopping... this halts step two.) 

- README.md # It's this readme.
- syn-installer0.sh # part one ~ Setup partitions, pacstrap packages and generate fstab.
- syn-installer1.sh # part two ~ Customize configration files, create users and enable services.

It's possible once the main and future-xibo* branches are complete these script will: 
- Be simple to clone the main branch to make changes to the installer script before running it.
- Have a clear folder structure so copying configuration files is easier to intergrate into the script.
- Have more variation in the deployment tasks being crafted.
- Provide a basic framework for creating other meta-distrobutions
 

The folders included are currenty not required at all for SYN-RTOS.

# /default-dotfiles
Contains all the required dotfiles to rebuild the system and provide the created user with a desktop enviroment. This can be adapted.

./config -- Contains configuration files for software inluced such as:
- openbox (window manager, uses xorg)
- qt5ctl (required for themeing on QT apps)
- tint2 (bar at top of screen)
- kwriterc (rc files for kwrite text editor)

./oh-my-zsh -- Dumping ground for oh-my-zsh extentions

Other files included:
- .profile -- export QT_QPA_PLATFORMTHEME="qt5ct"
- .xinitrc -- startx enviroment stuff
- .zshrc -- resultant file after installing oh-my-zsh

# /network_scripts
Some network related scripts

- eno1-wlan0_bridge.sh # will provide an ethernet port with a route to the internet. 
- ip-configure.sh # which will set an IP if you can't use DHCP and are lazy.

# /xibo-related
Archiso files used for an automated Digital-signage player. 
- Some custom dotfiles as well as chromium stuff for autostart. (be mindful of home dir and default path for openbox autostart)
- SDDM to autologin needs configuring so default sddm config does not need manual invervention.
- Boot has problem that it needs UUID defined in arch.conf.
- Boot has problem that loader.conf does not auto-appear
- The ttf-bitstream-vera font needs to be installed or Xibo picks strange fonts.
