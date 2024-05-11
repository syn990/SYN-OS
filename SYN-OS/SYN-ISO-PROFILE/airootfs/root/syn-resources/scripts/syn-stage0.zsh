#!/bin/zsh

# SYN-OS
# SYNTAX990
# William Hayward-Holland
# MIT License

# - syn-stage0.zsh

###############################################################################################################
# IMPORTANT NOTICE!                           # IMPORTANT NOTICE!                           # IMPORTANT NOTICE!


# IMPORTANT NOTICE!
# Disk Processing:
# This script currently performs a wipe operation on /dev/sda without any user prompt. It's crucial to create a device label during this process to enhance the predictability of bootctl.
# Before executing this script, it's imperative to run 'lsblk' to inspect the disk layout and ensure correctness.
# If you're installing on a system with a Master Boot Record (MBR), carefully identify the disk name and verify that it matches the output from 'lsblk'. Failure to do so may result in boot failures.

# IMPORTANT NOTICE!
# Pacstrap Function:
# This function synchronizes packages using pacstrap. It is designed to streamline the installation process by categorizing packages into various arrays based on their functionality. These arrays are then combined into a single variable called SYNSTALL, which represents all the packages to be installed on the system. Before running this function, ensure that the necessary packages are included in the respective arrays as per your requirements.

    # Explanation of Arrays:
        # - The basePackages array includes fundamental packages essential for the system's core functionality. This includes base system utilities, compilers, and essential firmware.
        # - The systemPackages array consists of packages related to system utilities and services, such as network management tools, audio utilities, and display managers.
        # - The controlPackages array contains packages for controlling various aspects of the system, such as display settings and audio control.
        # - The wmPackages array includes packages related to window managers and the X server, essential for graphical user interface (GUI) functionality.
        # - The cliPackages array comprises command-line interface (CLI) utilities commonly used for system administration and everyday tasks.
        # - The guiPackages array contains graphical user interface (GUI) applications for enhancing the user experience.
        # - The fontPackages array includes font-related packages to ensure proper font rendering and compatibility.
        # - The cliExtraPackages array includes additional CLI utilities for specialized tasks or user preferences.
        # - The guiExtraPackages array comprises extra GUI applications for specific use cases or user preferences.

    # Combining Arrays into SYNSTALL:
        # All these arrays are combined into a single variable called SYNSTALL using the following syntax:
        # SYNSTALL=($basePackages $systemPackages $controlPackages $wmPackages $cliPackages $guiPackages $fontPackages $cliExtraPackages $guiExtraPackages)
        # This ensures that all the packages from different categories are consolidated into one variable for ease of use during the pacstrap operation.

    # Usage:
        # Once the SYNSTALL variable is defined, it is passed as an argument to the pacstrap command along with the root mount location. For example:
        # pacstrap -K $ROOT_MOUNT_LOCATION_990 $SYNSTALL
        # This command installs all the packages listed in the SYNSTALL variable to the specified root mount location, ensuring that the necessary software components are installed for the resulting system.

    # Customization:
        # If you wish to add or remove packages, you can modify the respective arrays according to your requirements. Simply add or remove package names from the relevant array, ensuring that they are appropriately categorized. Once the arrays are updated, the SYNSTALL variable will automatically reflect the changes, allowing for flexible customization of the installation process.

# IMPORTANT NOTICE!
# Dotfiles and Variables Setup:
# This function handles the copying of system configuration files and variables necessary for setting up the environment in the new root directory. It ensures that essential configuration files are available to users, enabling a consistent and complete user experience.

    # Copying Dotfiles:
        # - The function copies dotfiles from the DotfileOverlay directory to the new root directory (/mnt). These dotfiles include configuration files for various applications and utilities, ensuring that default settings are applied to the system.

    # Setting Variables:
        # - Additionally, the function copies the syn-stage0.zsh script to the root directory. This script contains essential variables and functions required for the installation process. By duplicating it to the root directory, we ensure that these variables are accessible and correctly set up during subsequent stages of installation.

    # Filesystem Table Generation:
        # - After copying dotfiles and variables, the function generates the filesystem table (/etc/fstab) with boot information regarding UUID assignment. This step is crucial for ensuring proper booting and system functionality, as it defines how partitions are mounted and accessed by the system.

    # User Dotfiles:
        # - Lastly, the function ensures that user dotfiles are copied to /etc/skel, ensuring that new users created on the system inherit a complete environment with predefined settings and configurations.

    # Customization:
        # - Users can customize dotfiles and variables according to their preferences or specific system requirements. Simply modify the files in the DotfileOverlay directory or update the syn-stage0.zsh script as needed to reflect desired changes in configuration.

    # Usage:
        # - To utilize this function, simply call it within the main script. It will automatically handle the copying of dotfiles, setting up variables, generating the filesystem table, and ensuring user dotfiles are available for new users.

    # Verification:
        # - After execution, users can verify the presence and correctness of copied dotfiles and variables in the new root directory (/mnt). Additionally, inspecting the generated filesystem table (/mnt/etc/fstab) ensures that partitions are correctly defined for mounting during boot.


############################################################################################################

# Below, you'll find all the variables used in syn-stage0.zsh.
# Modify them for your convenience or use your own partitioning strategy.

# Disk Mount and Partition Variables
WIPE_DISK_990="/dev/sda"                  # The drive to be wiped - primary storage medium.
BOOT_PART_990="/dev/sda1"                 # The boot partition.
ROOT_PART_990="/dev/sda2"                 # The root partition.
BOOT_MOUNT_LOCATION_990="/mnt/boot"       # The mount point for the boot directory.
ROOT_MOUNT_LOCATION_990="/mnt/"           # The mount point for the root directory.
BOOT_FILESYSTEM_990="fat32"               # The filesystem format for the boot partition. If changed, ensure to update corresponding disk processing variables below.
ROOT_FILESYSTEM_990="f2fs"                # The filesystem format for the root partition. If changed, ensure to update corresponding disk processing variables below.

# Function to check if a command executed successfully
check_success() {
    if [ $? -ne 0 ]; then
        echo "\033[1;31mError: $1\033[0m"
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

# Function ti prepare the environment for SYN-OS
syn-os_environment_prep() {
    loadkeys uk                               # Setup the keyboard layout
    check_success "Failed to setup keyboard layout"
    timedatectl set-ntp true                  # Setup NTP so the time is up-to-date
    check_success "Failed to set NTP"
    systemctl start dhcpcd.service            # Setup DHCP on boot
    check_success "Failed to start DHCP service"
}

# Function to display an ASCII art threat before wiping disk
wipe_art_montage() {
    echo "\033[0;31m____    __    ____  __  .______    __  .__   __.   _______ \033[0m"
    echo "\033[0;31m\   \  /  \  /   / |  | |   _  \  |  | |  \ |  |  /  _____|\033[0m"
    echo "\033[0;31m \   \/    \/   /  |  | |  |_)  | |  | |   \|  | |  |  __  \033[0m"
    echo "\033[0;31m  \            /   |  | |   ___/  |  | |  .    | |  | |_ | \033[0m"
    echo "\033[0;31m   \    /\    /    |  | |  |      |  | |  |\   | |  |__| | \033[0m"
    echo "\033[0;31m    \__/  \__/     |__| | _|      |__| |__| \__|  \______| \033[0m"
    echo "\033[0;31m ___________    ____  _______ .______     ____    ____ .___________. __    __   __  .__   __.   _______ \033[0m"
    echo "\033[0;31m|   ____\   \  /   / |   ____||   _  \    \   \  /   / |           ||  |  |  | |  | |  \ |  |  /  _____|\033[0m"
    echo "\033[0;31m|  |__   \   \/   /  |  |__   |  |_)  |    \   \/   /   ---|  |---- |  |__|  | |  | |   \|  | |  |  __  \033[0m"
    echo "\033[0;31m|   __|   \      /   |   __|  |      /      \_    _/       |  |     |   __   | |  | |  .    | |  | |_ | \033[0m"
    echo "\033[0;31m|  |____   \    /    |  |____ |  |\  \----.   |  |         |  |     |  |  |  | |  | |  |\   | |  |__| | \033[0m"
    echo "\033[0;31m|_______|   \__/     |_______|| _|  ._____|   |__|         |__|     |__|  |__| |__| |__| \__|  \______| \033[0m"
    echo ""
    echo "\033[1;31mIf you didn't read the source properly, you may risk wiping all your precious data...\033[0m"
    echo
    echo "Press CTRL + C Right NOW IF YOU WANT TO SAVE YOUR DATA YOU HAVE LESS THAN A SECOND TO REACT"
    sleep 3
}

# Function to process disks for UEFI systems
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

# Function to process disks for MBR systems
disk_processing_mbr() {
  # Check if EFI System Partition (ESP) exists
    if [ ! -d "/sys/firmware/efi/efivars" ]; then
        echo "MBR System detected."
        echo "Disk processing for MBR systems..."

        # Wipe the disk and create a single partition
        echo "Wiping disk: $WIPE_DISK_990 and creating a single partition"
        parted --script $WIPE_DISK_990 mklabel msdos
        check_success "Failed to create disk label"

        parted --script $WIPE_DISK_990 mkpart primary ext4 1MiB 100%
        check_success "Failed to create partition"

        # Format the partition with ext4 filesystem
        echo "Formatting partition: $BOOT_PART_990 with ext4 filesystem"
        mkfs.ext4 $BOOT_PART_990
        check_success "Failed to format partition"

        # Mount the partition
        echo "Mounting partition: $BOOT_PART_990 to $ROOT_MOUNT_LOCATION_990"
        mount $BOOT_PART_990 $ROOT_MOUNT_LOCATION_990
        check_success "Failed to mount partition"
    else
        echo "EFI System detected. Skipping Syslinux installation."
    fi
}

# Function to display ASCII art for SYN-OS
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

# Function to synchronize pacstrap packages
pacstrap_sync() {
    sleep 1
    echo  "\033[0;34m ___  _   ___ ___ _____ ___    _   ___ \033[0m"
    echo  "\033[0;34m| _ \/_\ / __/ __|_   _| _ \  /_\ | _ |""\033[0m"
    echo  "\033[0;34m|  _/ _ \ (__\__ \ | | |   / / _ \|  _/\033[0m"
    echo  "\033[0;34m|_|/_/ \_\___|___/ |_| |_|_\/_/ \_\_|  \033[0m"
    echo ""
    sleep 0.5 
    echo "Installing packages to the resulting system."
    sleep 1
    echo ""
    echo -e "\033[0;33m   ,##.                   ,==.\033[0m"
    echo -e "\033[0;33m ,#    #.                 \\ o '\033[0m"
    echo -e "\033[0;33m#        #     _     _     \\    \\\033[0m"
    echo -e "\033[0;33m#        #    (_)   (_)    /    ;\033[0m"
    echo -e "\033[0;33m \`#    #'                 /   .'\033[0m"
    echo -e "\033[0;33m   \`##'                   \"==\"\033[0m"
    sleep 1

    # Define arrays for different categories of packages
    basePackages=("base" "base-devel" "dosfstools" "fakeroot" "gcc" "linux" "linux-firmware" "archlinux-keyring" "pacman-contrib" "sudo" "zsh")
    systemPackages=("alsa-utils" "archlinux-xdg-menu" "dhcpcd" "dnsmasq" "hostapd" "iwd" "pulseaudio" "python-pyalsa" "syslinux")
    controlPackages=("lxrandr" "obconf-qt" "pavucontrol-qt")
    wmPackages=("openbox" "qt5ct" "xcompmgr" "xorg-server" "xorg-xinit" "tint2")
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

# Function to copy dotfiles and setup variables
dotfiles_and_vars() {
    echo "Generating filesystem table with boot information in respect to UUID assignment"
    genfstab -U /mnt >> /mnt/etc/fstab
    if [ $? -ne 0 ]; then
        echo "❌ Failed to generate filesystem table"
        exit 1
    else
        echo "✅ Filesystem table generated successfully"
    fi

    echo ""
    echo "\033[1;34m🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟\033[0m"
    echo "\033[1;34m🚀    SYN-OS Stage 0: Setting Up Dotfiles        🚀\033[0m"
    echo "\033[1;34m🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟🌟\033[0m"
    echo ""

    # Copy the dotfiles to the new root directory
    echo "📂 Copying dotfiles to the new root directory..."
    cp -Rv /root/syn-resources/DotfileOverlay/* "$ROOT_MOUNT_LOCATION_990/"
    if [ $? -ne 0 ]; then
        echo "❌ Failed to copy dotfiles to the new root directory"
        exit 1
    else
        echo "✅ Dotfiles copied successfully"
    fi

    # The file is duplicated to the root directory as stage 0 relies on its source for the partition vars.
    cp -v /root/syn-resources/scripts/syn-stage0.zsh $ROOT_MOUNT_LOCATION_990/syn-stage0.zsh
    if [ $? -ne 0 ]; then
        echo "❌ Failed to stage 0"
        exit 1
    else
        echo "✅ Stage 0 almost completed..."
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
        echo "❌ Failed to copy stage 1"
        exit 1
    else
        echo "✅ Stage 1 completed successfully"
    fi
}

# Function to display end ASCII art and summary
end_art() {
    
    clear
    
    echo ""
    echo "     \033[0;31m _______.____    ____ .__   __.          ______        _______.\033[0m"
    echo "    \033[0;31m/       |\   \  /   / |  \ |  |         /  __  \      /       |\033[0m"
    echo "   \033[0;31m|   (----  \   \/   /  |   \|  |  ______|  |  |  |    |   (---- \033[0m"
    echo "    \033[0;31m\   \      \_    _/   |  .    | |______|  |  |  |     \   \    \033[0m"
    echo "\033[0;31m.----)   |       |  |     |  |\   |        |   --'  | .----)   |   \033[0m"
    echo "\033[0;31m|_______/        |__|     |__| \__|         \______/  |_______/    \033[0m"

    sleep 0.5
    
    echo ""
    echo "\033[32mSUMMARY: Stage Zero Complete - Prepare for the next steps\033[0m"
    
    sleep 0.2
    
    echo ""
    echo "\033[1;37mCongratulations! Stage Zero of the process is complete.\033[0m"
    echo ""
    
    sleep 0.2
    
    echo "\033[32m• \033[1;37mMounted the root partition (\033[1;93m$ROOT_PART_990\033[0m\033[1;37m) to the root directory (\033[1;93m$ROOT_MOUNT_LOCATION_990\033[0m\033[1;37m).\033[0m"
    echo "\033[32m• \033[1;37mCreated the boot partition (\033[1;93m$BOOT_PART_990\033[0m\033[1;37m) and format it with the appropriate filesystem (\033[1;93m$BOOT_FILESYSTEM_990\033[0m\033[1;37m).\033[0m"
    echo "\033[32m• \033[1;37mFormatted the root partition (\033[1;93m$ROOT_PART_990\033[0m\033[1;37m) with the appropriate filesystem (\033[1;93m$ROOT_FILESYSTEM_990\033[0m\033[1;37m).\033[0m"
    echo "\033[32m• \033[1;37mMounted the boot partition (\033[1;93m$BOOT_PART_990\033[0m\033[1;37m) to the boot directory (\033[1;93m$BOOT_MOUNT_LOCATION_990\033[0m\033[1;37m).\033[0m"
    echo "\033[32m• \033[1;37mGenerated the filesystem table with boot information in respect to UUID assignment.\033[0m"
    echo "\033[32m• \033[1;37mInstalled the essential packages to the resulting system using Pacstrap.\033[0m"
    echo "\033[32m• \033[1;37mGenerated cryptographic keys for Pacman and update the package databases.\033[0m"
    echo "\033[32m• \033[1;37mStrapped the SYN-OS patched files into the root directory of the system.\033[0m"
    echo "\033[32m• \033[1;37mCopied the user dotfiles into /etc/skel ensuring new users get complete environment.\033[0m"
    echo ""
    echo ""
    sleep 5
}

# The functions will execute in the exact order they are listed.

syn-os_environment_prep  # Configures essential system settings: keyboard layout, NTP, DHCP.
wipe_art_montage         # Displays ASCII art to emphasize filesystem manipulation consequences.
disk_processing_uefi     # Handles disk formatting and partitioning for UEFI systems with bootctl bootloader.
disk_processing_mbr      # Manages disk formatting and partitioning for MBR systems with SYSLINUX bootloader.
art_montage              # Presents symbolic artwork representing SYN-OS's creative autonomy.
pacstrap_sync            # Installs a predefined set of packages for system functionality.
dotfiles_and_vars        # Copies system configuration files and variables for setup and customization.
end_art                  # Displays concluding ASCII art, marking stage completion with a celebratory message.

echo "Executing syn-stage1.zsh script in the new root directory."
arch-chroot $ROOT_MOUNT_LOCATION_990 /bin/zsh -c "chmod +x /syn-stage0.zsh; chmod +x /syn-stage1.zsh; /syn-stage1.zsh"
