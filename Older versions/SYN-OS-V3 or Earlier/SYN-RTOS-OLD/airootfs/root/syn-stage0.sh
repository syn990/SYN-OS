#!/bin/sh

# Read this script, make suitable ammendments before executing.
# If you run this script without considering it's implications you wil destroy your system.

# Future ambitions include setting up non-standard file-system such as xfs, zfs 
# or btrfs for block-device backups instead of individual files.

# Encryption is a real possibility to be baked in both the live and
# persistent enviroment.

	loadkeys uk								# Setup the keyboard layout
	timedatectl set-ntp true						# Setup NTP so the time is up-to-date
	systemctl start dhcpcd.service						# Setup DHCP on boot (seems to need manually doing recently, perhaps releng is borked)

	clear

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "____    __    ____  __  .______    __  .__   __.   _______ "
echo "\   \  /  \  /   / |  | |   _  \  |  | |  \ |  |  /  _____|"
echo " \   \/    \/   /  |  | |  |_)  | |  | |   \|  | |  |  __  "
echo "  \            /   |  | |   ___/  |  | |  .    | |  | |_ | "
echo "   \    /\    /    |  | |  |      |  | |  |\   | |  |__| | "
echo "    \__/  \__/     |__| | _|      |__| |__| \__|  \______| "
echo " ___________    ____  _______ .______     ____    ____ .___________. __    __   __  .__   __.   _______ "
echo "|   ____\   \  /   / |   ____||   _  \    \   \  /   / |           ||  |  |  | |  | |  \ |  |  /  _____|"
echo "|  |__   \   \/   /  |  |__   |  |_)  |    \   \/   /   ---|  |---- |  |__|  | |  | |   \|  | |  |  __  "
echo "|   __|   \      /   |   __|  |      /      \_    _/       |  |     |   __   | |  | |  .    | |  | |_ | "
echo "|  |____   \    /    |  |____ |  |\  \----.   |  |         |  |     |  |  |  | |  | |  |\   | |  |__| | "
echo "|_______|   \__/     |_______|| _|  ._____|   |__|         |__|     |__|  |__| |__| |__| \__|  \______| "
echo ""
echo "If you didn't read the source properly you've probably wiped all your precious data..."                                                        
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

# This currently wipes sda, without any prompt. We need it to create a device lable as this will help bootctl be more predictable						   
# It's set to gpt so a different script is needed for MBR setups. 

	WIPE_DISK_990="/dev/sda"	# Drive to be wiped - The main storage medium
	BOOT_PART_990="/dev/sda1"	# The boot partition
	ROOT_PART_990="/dev/sda2"	# The root partition
	BOOT_DATA_990="/mnt/boot"	# The boot directory
	ROOT_DATA_990="/mnt/"		# The root directory

	parted --script $WIPE_DISK_990 mklabel gpt mkpart primary fat32 1Mib 200Mib set 1 boot on
	parted --script $WIPE_DISK_990 mkpart primary ext4 201Mib 100%
	mkfs.vfat -F 32 $BOOT_PART_990
        mkfs.ext4 -F $ROOT_PART_990
        	mount $ROOT_PART_990 $ROOT_DATA_990
			mkdir $BOOT_DATA_990
			mount $BOOT_PART_990 $BOOT_DATA_990
 
echo "     _______.____    ____ .__   __.        .______     .___________.  ______        _______."
echo "    /       |\   \  /   / |  \ |  |        |   _  \    |           | /  __  \      /       |"
echo "   |   (----  \   \/   /  |   \|  |  ______|  |_)  |    ---|  |---- |  |  |  |    |   (---- "
echo "    \   \      \_    _/   |  .    | |______|      /        |  |     |  |  |  |     \   \    "
echo ".----)   |       |  |     |  |\   |        |  |\  \----.   |  |     |   --'  | .----)   |   "
echo "|_______/        |__|     |__| \__|        | _|  ._____|   |__|      \______/  |_______/    "
echo "~~~~~~~~~~LOADING~~~~~~~~~~~~~~~~~~~~~~LOADING~~~~~~~~~~~~~~~~~~~~LOADING~~~~~~~~~~~~~~~~~~~"

# You can add/remove packages in these variables. It's done this way so you can see what's being installed.
# The variables are meaningless, pacstrap does not care.
# This implementation means you can modify the script and even ommit entire sections conveniently.
#
# All packages are installed in a single pacstrap command, allowing a total-size prediction for all packages during install.
# Ensure the package name is valid, and pacstrap will install it.

	BASE_990="base base-devel dosfstools fakeroot gcc linux linux-firmware pacman-contrib sudo zsh"
	SYSTEM_990__="alsa-utils archlinux-xdg-menu dhcpcd dnsmasq hostapd iwd pulseaudio python-pyalsa"
	CONTROL_990_="lxrandr obconf-qt pavucontrol-qt"
	WM_990______="openbox xcompmgr xorg-server xorg-xinit tint2"
	CLI_990_____="git htop man nano reflector rsync wget"
	GUI_990_____="engrampa feh kitty kwrite pcmanfm-qt"
	FONT_990____="terminus-font ttf-bitstream-vera"
	CLI_XTRA_990="android-tools archiso binwalk brightnessctl hdparm hexedit lshw ranger sshfs yt-dlp"	
	GUI_XTRA_990="audacity chromium gimp kdenlive obs-studio openra spectacle vlc"
	VM_XTRA_990_="edk2-ovmf libvirt qemu-desktop virt-manager virt-viewer"
	
	SYNSTALL="$BASE_990 $SYSTEM_990__ $CONTROL_990_ $WM_990______ $CLI_990_____ $GUI_990_____ $FONT_990____ $CLI_XTRA_990 $GUI_XTRA_990 $VM_XTRA_990_"

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo  " ___  _   ___ ___ _____ ___    _   ___ "
echo  "| _ \/_\ / __/ __|_   _| _ \  /_\ | _ |"""
echo  "|  _/ _ \ (__\__ \ | | |   / / _ \|  _/"
echo  "|_|/_/ \_\___|___/ |_| |_|_\/_/ \_\_|  ~~~~~~~~~~~"
echo ""
echo "Installing packages to the resulting system."
echo "Applying mirror mystics and re-securing the keyring"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

# If you wanted to add your own packages:
# Add packages after $SYNSTALL like this "pacstrap /mnt $SYNSTALL firefox mixxx virtualbox some-other-package"

	reflector -c "GB" -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist 
	
		pacman -Sy			#DUPLICATE
		pacman-key --init
		pacman-key --populate archlinux						
		pacman -Sy			#DUPLICATE
	
	pacstrap /mnt $SYNSTALL									# This is the pacstrap that combines all the packages in the variables above

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo " _______  _______  __    _  _______  _______  _______  _______  _______ "
echo "|       ||       ||  |  | ||       ||       ||       ||   _   ||  _    |"
echo "|    ___||    ___||   |_| ||    ___||  _____||_     _||  |_|  || |_|   |"
echo "|   | __ |   |___ |       ||   |___ | |_____   |   |  |       ||       |"
echo "|   ||  ||    ___||  _    ||    ___||_____  |  |   |  |       ||  _   | "
echo "|   |_| ||   |___ | | |   ||   |     _____| |  |   |  |   _   || |_|   |"
echo "|_______||_______||_|  |__||___|    |_______|  |___|  |__| |__||_______|~~~~~~~~"
echo ""
echo  "Generating filesystem table with boot information in respect to UUID assignment"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

	genfstab -U /mnt >> /mnt/etc/fstab 			

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo  "  ____   ___ _____ _____ ___ _     _____ ____  "
echo  " |  _ \ / _ \_   _|  ___|_ _| |   | ____/ ___| "
echo  " | | | | | | || | | |_   | || |   |  _| \___ \ "
echo  " | |_| | |_| || | |  _|  | || |___| |___ ___) |"
echo  " |____/ \___/ |_| |_|   |___|_____|_____|____/ ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo ""
echo  " Copying the 1.root_filesystem_overlay materials to the result system root directory"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"

	ROOTFSOVERLAY990="/root/SYN-RTOS-V3/1.root_filesystem_overlay/*"
	
	cp -R $ROOTFSOVERLAY990 /mnt/					# Copy these files to the root filesystem.
	cp -R /root/syn-stage1.sh /mnt/root/syn-stage1.sh		# Copy the stage one script to the new root home directory.
	
	clear

echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "     _______.____    ____ .__   __.        .______     .___________.  ______        _______."
echo "    /       |\   \  /   / |  \ |  |        |   _  \    |           | /  __  \      /       |"
echo "   |   (----  \   \/   /  |   \|  |  ______|  |_)  |    ---|  |---- |  |  |  |    |   (---- "
echo "    \   \      \_    _/   |  .    | |______|      /        |  |     |  |  |  |     \   \    "
echo ".----)   |       |  |     |  |\   |        |  |\  \----.   |  |     |   --'  | .----)   |   "
echo "|_______/        |__|     |__| \__|        | _|  ._____|   |__|      \______/  |_______/    "
echo ""
echo "Stage Zero Complete - You now need to arch-chroot into the new system to continue building."
echo ""
echo "The root file system was mounted on $ROOT_DATA_990, change the root directory:
echo ""arch-chroot /mnt"""
echo ""
echo "Once that is done you will need to run syn-stage1.sh"
echo "Ensure you make ammendments to the variables in the script to ensure the intended result system"
echo "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
echo "-	Filesystems created"
echo "-	Partitions mounted"
echo "-	Pacstrap completed"
echo "-	Fstab generated"
echo "-	cp -R $ROOTFSOVERLAY990 /mnt/ was successful"
	
