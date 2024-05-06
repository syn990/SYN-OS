#!/bin/zsh

# SYN-OS
# SYNTAX990
# WILLIAM HAYWARD-HOLLAND
# M.I.T LICENSE

# - syn-stage0.zsh

############################################################################################################

# Below you will find all the mount and partition variables, which are used in syn-stage0.zsh
# Modify them for convenience, or use your own partitioning.

# This currently wipes sda, without any prompt. We need it to create a device label as this will help bootctl be more predictable
# You absolutely must run lsblk and check the disk
# It's set to gpt so a different script is needed for MBR setups.

WIPE_DISK_990="/dev/sda"                  # Drive to be wiped - The main storage medium
BOOT_PART_990="/dev/sda1"                 # The boot partition
ROOT_PART_990="/dev/sda2"                 # The root partition
BOOT_MOUNT_LOCATION_990="/mnt/boot"       # The boot directory
ROOT_MOUNT_LOCATION_990="/mnt/"           # The root directory
BOOT_FILESYSTEM_990="fat32"               # The boot partition's filesystem
ROOT_FILESYSTEM_990="f2fs"                # The root partition's filesystem

# Function to check if a command executed successfully
check_success() {
    if [ $? -ne 0 ]; then
        echo "Error: $1"
        exit 1
    fi
}

# Function to display a warning message and confirm the action
confirm_action() {
    local message=$1
    echo -e "\n$message"
    read -p "Do you want to proceed? (y/n) " confirm
    if [ "$confirm" != "y" ]; then
        echo "Action terminated."
        exit 0
    fi
}

syn-os_environment_prep() {
    loadkeys uk                               # Setup the keyboard layout
    check_success "Failed to setup keyboard layout"
    timedatectl set-ntp true                  # Setup NTP so the time is up-to-date
    check_success "Failed to set NTP"
    systemctl start dhcpcd.service            # Setup DHCP on boot
    check_success "Failed to start DHCP service"
}

disk_processing_uefi() {
    # Check if EFI System Partition (ESP) exists
    if [ -d "/sys/firmware/efi/efivars" ]; then
        echo "EFI System detected."
        echo "WARNING: Wiping disk: $WIPE_DISK_990 and creating a boot partition on $BOOT_PART_990"
        parted --script $WIPE_DISK_990 mklabel gpt mkpart primary $BOOT_FILESYSTEM_990 1Mib 200Mib set 1 boot on
        check_success "Failed to create boot partition"

        echo "Creating root partition: $BOOT_PART_990"
        parted --script $WIPE_DISK_990 mkpart primary $ROOT_FILESYSTEM_990 201Mib 100%
        check_success "Failed to create root partition"

        echo "Formatting boot partition: $BOOT_PART_990"
        mkfs.vfat -F 32 $BOOT_PART_990
        check_success "Failed to format boot partition"

        echo "Formatting root partition: $ROOT_PART_990"
        mkfs.f2fs -f $ROOT_PART_990
        check_success "Failed to format root partition"

        echo "Mounting root partition: $ROOT_PART_990 to $ROOT_MOUNT_LOCATION_990"
        mount $ROOT_PART_990 $ROOT_MOUNT_LOCATION_990
        check_success "Failed to mount root partition"

        echo "Creating boot directory: $BOOT_MOUNT_LOCATION_990"
        mkdir $BOOT_MOUNT_LOCATION_990
        check_success "Failed to create boot directory"

        echo "Mounting boot partition: $BOOT_PART_990 to $BOOT_MOUNT_LOCATION_990"
        mount $BOOT_PART_990 $BOOT_MOUNT_LOCATION_990
        check_success "Failed to mount boot partition"
    else
        echo "MBR System detected. Skipping EFI partition creation."
    fi
}

disk_processing_mbr() {
    # Check if EFI System Partition (ESP) exists
    if [ ! -d "/sys/firmware/efi/efivars" ]; then
        echo "MBR System detected."

        echo "Disk processing for MBR systems..."

        # Wipe the disk and create a single partition
        echo "Wiping disk: $WIPE_DISK_990 and creating a single partition"
        parted --script $WIPE_DISK_990 mklabel msdos
        check_success "Failed to create disk label"

        parted --script $WIPE_DISK_990 mkpart primary $ROOT_FILESYSTEM_990 1Mib 100%
        check_success "Failed to create root partition"

        # Format the root/boot partition
        echo "Formatting root partition: $BOOT_PART_990"
        mkfs.f2fs -f $BOOT_PART_990
        check_success "Failed to format root partition"

        # Mount the root/boot partition
        echo "Mounting root partition: $BOOT_PART_990 to $ROOT_MOUNT_LOCATION_990"
        mount $BOOT_PART_990 $ROOT_MOUNT_LOCATION_990
        check_success "Failed to mount root partition"
    else
        echo "EFI System detected. Skipping MBR partition creation."
        echo "Something terrible has happened."
        sleep 990
    fi
}

art_montage() {
    clear

    printf "\e[1;31m~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
    printf "     _______.____    ____ .__   __.          ______        _______.\n"
    printf "    /       |\   \  /   / |  \ |  |         /  __  \      /       |\n"
    printf "   |   (----  \   \/   /  |   \|  |  ______|  |  |  |    |    ---- \n"
    printf "    \   \      \_    _/   |  .    | |______|  |  |  |     \   \    \n"
    printf ".----    |       |  |     |  |\   |        |  \`--'  | .----    |   \n"
    printf "|_______/        |__|     |__| \__|         \______/  |_______/    \n"
    printf "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\e[0m\n\n"
    sleep 1
    echo "Without constraints; SYN-OS has independent freedom/volition and creative intelligence, actively participating in the ongoing creation of reality"
    sleep 0.1
    echo ""
    echo "(((((((((((((((((((((((((/((((((((/***//////////////////////////////////////////"
    echo "(((((((((((((((((((((((((/**(((/*******/////////////////////////////////////////"
    echo "((((((((((((((((((((((((((***,,,,,,,,,,,,,,,,*****//////////////////////////////"
    echo "((((((((((((((((((/********,,,,,,,,,,,,,,,,,,,,,,**/////////////////////////////"
    echo "(((((((((((((((/****,,**,**,,,,,,,,,,,,,,,,,,,,,,,,****/////////////////////////"
    echo "((((((((((((/****,,,,,,,,,,,,,,,,*,,,,,,,,,,,,,,,********///////////////////////"
    echo "((((((((((**********,,,*/******,,,,*,,,,*,,,,,,,,,,,,**/////////////////////////"
    echo "(((((((((********,,,**///(((((((((//**,,,,,,,,,,,,,,,,,,*///////////////////////"
    echo "(((((((((******,,,,*/((((((((((/(//*****,,,,,,,,,,,,,,***/**/////////////////////"
    echo "(((((((//****,,,,,,/((((((((((//(*****,,,,,,,,,,,,,,***/**/////////////////////"
    echo "((((((((/****,,,***(((((((/*****(*********,,,,,,,,,,,,,,,*//////////////////////"
    echo "(((((((((/********(((((/###(((/****//(/(//*******,,,,,,,,**/////////////////////"
    echo "(((((((((********((((((((*,..*(/(/**/(*((***,,,,,*,,,,,,,,,***//////////////////"
    echo "(((((((((((******(((((///(((//***/////*,....,.,**/,,,,*/////////////////////////"
    echo "((((((((((((/(***((((((/////////////*****,******/*,,,*//////////////////////////"
    echo "((((((((((((/**/*((((((((///////(((((//*//******/*,,**//////////////////////////"
    echo "((((((((((((/*((*(((##(((/((((((///////**////***/**/////////////////////////////"
    echo "((((((((((((/*/***(((((((((((////,,(((/****//////,//////////////////////////////"
    echo "((((((((((((((/*****/(((((((((//*,,,,,,,***//////*///////////*//////////////////"
    echo "((((((((((((((********(((((***,,,,,,,,,,,,**///////////////****/////////////////"
    echo "((((((((((((((**********/(/**/////*****,,*******///////////****//////////////***"
    echo "(((((((((((((((*,,,,,,****,////////,,,,****,*,,////////////*****/*////////******"
    echo "(((((((((((((((//*,,,,,,,,,,/////*,..,*****,,,*//////////*******///////*********"
    echo "(((((((((((((((///*,,,,,,,,,*///*,,,,,****,,,*//////////********/////***********"
    echo "(((((((((((((////*****,,,,,,,,,,,,,,,,.,,,,**///////////*******///**************"
    echo "((((((((((///**//*******,..............,,,****//////////****/////***************"
    echo "((((((///*****************,,,........,**********//////////////******************"
    echo "(((//**********************,,,,,,,,,*****************////*/*********************"
    echo "*****************************,,,********************************/*******/*******"
    echo "**,,*,*******,*************************************************//***************"
    echo "*,,,,,***********,****************************************/****//***************"
    echo ",,,,,,,,,,*********,,,,,,,,,,***,****,,,,,,,******************//***************"
    echo ""
    sleep 1
    echo "SYN-OS has independent freedom/volition and creative intelligence..." 
    echo ""
    sleep 1
    echo "          .. actively participating in the ongoing creation of reality"
    echo "" 
    clear 
}

wipe_art_montage() {
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
    echo "If you didn't read the source properly, you may risk wiping all your precious data..."
    sleep 3
}

pacstrap_sync() {
    sleep 1
    echo  " ___  _   ___ ___ _____ ___    _   ___ "
    echo  "| _ \/_\ / __/ __|_   _| _ \  /_\ | _ |"""
    echo  "|  _/ _ \ (__\__ \ | | |   / / _ \|  _/"
    echo  "|_|/_/ \_\___|___/ |_| |_|_\/_/ \_\_|  "
    echo ""
    sleep 0.5 
    echo "Installing packages to the resulting system."
    sleep 1
    echo ""
    echo -e "\033[33m   ,##.                   ,==.\033[0m"
    echo -e "\033[33m ,#    #.                 \\ o '\033[0m"
    echo -e "\033[33m#        #     _     _     \\    \\\033[0m"
    echo -e "\033[33m#        #    (_)   (_)    /    ;\033[0m"
    echo -e "\033[33m \`#    #'                 /   .'\033[0m"
    echo -e "\033[33m   \`##'                   \"==\"\033[0m"
    sleep 1

    # Define arrays for different categories of packages
    basePackages=("base" "base-devel" "dosfstools" "fakeroot" "gcc" "linux" "linux-firmware" "archlinux-keyring" "pacman-contrib" "sudo" "zsh")
    systemPackages=("alsa-utils" "archlinux-xdg-menu" "dhcpcd" "dnsmasq" "hostapd" "iwd" "pulseaudio" "python-pyalsa" "syslinux")
    controlPackages=("lxrandr" "obconf-qt" "pavucontrol-qt")
    wmPackages=("openbox" "xcompmgr" "xorg-server" "xorg-xinit" "tint2")
    cliPackages=("git" "htop" "man" "nano" "reflector" "rsync" "wget")
    guiPackages=("engrampa" "feh" "kitty" "kwrite" "pcmanfm-qt")
    fontPackages=("terminus-font" "ttf-bitstream-vera")
    cliExtraPackages=("android-tools" "archiso" "binwalk" "brightnessctl" "hdparm" "hexedit" "lshw" "ranger" "sshfs" "yt-dlp")
    guiExtraPackages=("audacity" "chromium" "gimp" "kdenlive" "obs-studio" "openra" "spectacle" "vlc")
    #vmExtraPackages=("edk2-ovmf" "libvirt" "qemu-desktop" "virt-manager" "virt-viewer")

    # Combine arrays into a single array
    SYNSTALL=($basePackages $systemPackages $controlPackages $wmPackages $cliPackages $guiPackages $fontPackages $cliExtraPackages $guiExtraPackages)

    # Usage: pacstrap /mnt $SYNSTALL package1 package2 package 3 (you can use any package that is accessible from the mirrorlist)
    # This command installs all the packages listed in the SYNSTALL array to the specified mount point.

    # If you wanted to add your packages:
    # Add packages after $SYNSTALL like this "pacstrap /mnt $SYNSTALL firefox mixxx virtualbox some-other-package"

    pacstrap -K $ROOT_MOUNT_LOCATION_990 $SYNSTALL
    check_success "Failed to install packages to the new root directory."
}
############################################################################################################

dotfiles_and_vars() {
    echo "Generating filesystem table with boot information in respect to UUID assignment"
    genfstab -U /mnt >> /mnt/etc/fstab
    if [ $? -ne 0 ]; then
        echo "Failed to generate filesystem table"
        exit 1
    fi

    echo "  ____   ___ _____ _____ ___    _   ___ "
    echo " |  _ \ / _ \_   _|  ___|_ _| |   | ____/ ___| "
    echo " | | | | | | || | | |_   | || |   |  _| \___ \ "
    echo " | |_| | |_| || | |  _|  | || |___| |___ ___) |"
    echo " |____/ \___/ |_| |_|   |___|_____|_____|____/ "
    echo ""
    echo "Copying the dotfile overlay materials to the new system root directory"
    echo ""

    # Copy the dotfiles to /etc/skel in the new root directory
    echo "Copying dotfiles to /etc/skel in the new root directory"
    cp -Rv /root/syn-resources/DotfileOverlay/etc/skel "$ROOT_MOUNT_LOCATION_990/etc/"
    if [ $? -ne 0 ]; then
        echo "Failed to copy dotfiles to /etc/skel in the new root directory"
        exit 1
    fi

    # The file is duplicated to the root directory as stage 0 relies on its source for the partition vars.
    cp -v /root/syn-resources/scripts/syn-stage0.zsh $ROOT_MOUNT_LOCATION_990/syn-stage0.zsh
    if [ $? -ne 0 ]; then
        echo "Failed to stage 0"
        exit 1
    fi

    # Depending on whether the system boots with MBR or UEFI, choose the appropriate script
    if [ -d "/sys/firmware/efi/efivars" ]; then
        # UEFI system
        cp -v /root/syn-resources/scripts/syn-uefi.zsh $ROOT_MOUNT_LOCATION_990/syn-stage1.zsh
    else
        # MBR system
        cp -v /root/syn-resources/scripts/syn-mbr.zsh $ROOT_MOUNT_LOCATION_990/syn-stage1.zsh
    fi
    if [ $? -ne 0 ]; then
        echo "Failed to copy stage 1"
        exit 1
    fi
}

syslinux_setup_conditionally() {
    # Check if EFI System Partition (ESP) exists
    if [ ! -d "/sys/firmware/efi/efivars" ]; then
        # Create the syslinux folder if it doesn't exist
        echo "Creating syslinux folder if it doesn't exist"
        mkdir -p $BOOT_MOUNT_LOCATION_990/syslinux
        check_success "Failed to create syslinux folder"
        
        # Copy splash.png to the syslinux folder
        echo "Copying splash.png to the syslinux folder"
        cp /root/syn-resources/splash.png $BOOT_MOUNT_LOCATION_990/syslinux/syslinux.png
        check_success "Failed to copy splash.png to syslinux folder"

        echo "Installing Syslinux into disk MBR"
        extlinux --install /boot/syslinux
        umount $BOOT_PART_990
        dd bs=440 count=1 conv=notrunc if=/usr/lib/syslinux/bios/mbr.bin of=$WIPE_DISK_990 status=progress
        
        # Mount root partition
        echo "Mounting root partition: $BOOT_PART_990 to $ROOT_MOUNT_LOCATION_990"
        mount $BOOT_PART_990 $ROOT_MOUNT_LOCATION_990
        check_success "Failed to mount root partition"
    else
        echo "EFI System detected. Skipping SYSLINUX setup."
    fi
}

# Call syslinux_setup_conditionally function instead of syslinux-setup

end_art() {
    clear
    echo ""
    echo "     _______.____    ____ .__   __.          ______        _______."
    echo "    /       |\   \  /   / |  \ |  |         /  __  \      /       |"
    echo "   |   (----  \   \/   /  |   \|  |  ______|  |  |  |    |   (---- "
    echo "    \   \      \_    _/   |  .    | |______|  |  |  |     \   \    "
    echo ".----)   |       |  |     |  |\   |        |   --'  | .----)   |   "
    echo "|_______/        |__|     |__| \__|         \______/  |_______/    "
    echo ""
    echo ""
    echo "SUMMARY: Stage Zero Complete - Prepare for the next steps"
    echo ""
    echo "Congratulations! Stage Zero of the process is complete."
    echo ""
    echo "1. Mounted the root partition ($ROOT_PART_990) to the root directory ($ROOT_MOUNT_LOCATION_990)."
    echo "2. Created the boot partition ($BOOT_PART_990) and format it with the appropriate filesystem ($BOOT_FILESYSTEM_990)."
    echo "3. Formatted the root partition ($ROOT_PART_990) with the appropriate filesystem ($ROOT_FILESYSTEM_990)."
    echo "4. Mounted the boot partition ($BOOT_PART_990) to the boot directory ($BOOT_MOUNT_LOCATION_990)."
    echo "5. Generated the filesystem table with boot information in respect to UUID assignment."
    echo "6. Installed the essential packages to the resulting system using Pacstrap."
    echo "7. Applied mirror mystics and re-secure the keyring."
    echo "8. Generated cryptographic keys for Pacman and update the package databases."
    echo "9. Copied the root overlay materials from $DotfileOverlay to the root directory."
    echo "10. Completed Stage Zero now to arch-chroot into the new system."
    echo "11. (hint - if the prompt has halted)"
    echo "    Run: arch-chroot $ROOT_MOUNT_LOCATION_990"
    echo "12. Run: sh /syn-stage1.zsh"
    echo ""
    sleep 0.5
}

syn-os_environment_prep
wipe_art_montage
disk_processing_uefi
disk_processing_mbr
art_montage
pacstrap_sync
dotfiles_and_vars
syslinux_setup_conditionally
end_art



echo "Executing syn-stage1.zsh script in the new root directory."
arch-chroot $ROOT_MOUNT_LOCATION_990 /bin/zsh -c "chmod +x /syn-stage0.zsh; chmod +x /syn-stage1.zsh; /syn-stage1.zsh"

