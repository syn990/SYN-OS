#!/bin/sh

# Below you will find all the mount and partition variables, which are used in syn-stage0.sh
# Modify them for convienence, or use your own partitioning.

# This currently wipes sda, without any prompt. We need it to create a device label as this will help bootctl be more predictable
# You absolutely must run lsblk and check the disk
# It's set to gpt so a different script is needed for MBR setups.

WIPE_DISK_990="/dev/vda"                  # Drive to be wiped - The main storage medium
BOOT_PART_990="/dev/vda1"                 # The boot partition
ROOT_PART_990="/dev/vda2"                 # The root partition
BOOT_MOUNT_LOCATION_990="/mnt/boot"       # The boot directory
ROOT_MOUNT_LOCATION_990="/mnt/"           # The root directory
BOOT_FILESYSTEM_990="fat32"               # The boot partition's filesystem
ROOT_FILESYSTEM_990="f2fs"                 # The root partition's filesystem

# Below you will find all the packages used on the intial pacstrap before installing files to the main system.
# You can insert package names found in the repositories here, if you want them installed on the initial pacstrap.
# Or simply scroll down to the pacstrap and add addtional packages: "pacstrap /mnt $SYNSTALL *package_name_here*" 

# You can add/remove packages in these variables. It's done this way so you can see what's being installed.
# This implementation means you can modify the script and even omit entire sections conveniently.

# All packages are installed in a single pacstrap command, allowing a total-size prediction for all packages during install.
# Ensure the package name is valid, and the mirrors can be read, and pacstrap will install it.

BASE_990="base base-devel dosfstools fakeroot gcc linux linux-firmware pacman-contrib sudo zsh"
SYSTEM_990__="alsa-utils archlinux-xdg-menu dhcpcd dnsmasq hostapd iwd pulseaudio python-pyalsa"
CONTROL_990_="lxrandr obconf-qt pavucontrol-qt"
WM_990______="openbox xcompmgr xorg-server xorg-xinit tint2"
CLI_990_____="git htop man nano reflector rsync wget"
GUI_990_____="engrampa feh kitty kwrite pcmanfm-qt"
FONT_990____="terminus-font ttf-bitstream-vera"
CLI_XTRA_990="android-tools archiso binwalk brightnessctl hdparm hexedit lshw ranger sshfs yt-dlp"
GUI_XTRA_990="audacity chromium gimp kdenlive obs-studio openra spectacle vlc"
#VM_XTRA_990_="edk2-ovmf libvirt qemu-desktop virt-manager virt-viewer"

SYNSTALL="$BASE_990 $SYSTEM_990__ $CONTROL_990_ $WM_990______ $CLI_990_____ $GUI_990_____ $FONT_990____ $CLI_XTRA_990 $GUI_XTRA_990 $VM_XTRA_990_"


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

# Encryption is a real possibility to be baked in both the live and persistent environment.

loadkeys uk                               # Setup the keyboard layout
check_success "Failed to setup keyboard layout"

timedatectl set-ntp true                  # Setup NTP so the time is up-to-date
check_success "Failed to set NTP"

systemctl start dhcpcd.service            # Setup DHCP on boot
check_success "Failed to start DHCP service"

confirm_action "\n\nCRITICAL ALERT: Execution will cause IRRECOVERABLE LOSS on disk $WIPE_DISK_990. Proceed with caution.\n Comprehend consequences? This action is UNDOABLE."

confirm_action "\n\nNOTICE: William (Syntax990) assumes no responsibility for outcomes. NO WARRANTIES provided. You are accountable.\n\n Precautions taken? Ready to proceed?"

confirm_action "\n\nIMPORTANT: No manual intervention/edits to /root/syn-stage0.sh may render system unviable and irrevocably damaged. Proceed carefully.\n\n Are you absolutely certain you wish to continue? There will be NO turning back if you proceed."

read -p "Would you like to perform a 'dry run' to see which disk will be wiped without actually wiping it? (y/n) " confirm

if [ "$confirm" == "y" ]; then
    echo "Performing dry run. The following disk would be wiped:"
    echo $WIPE_DISK_990
    echo "Modify the values at the top of the syn-stage0.sh script to ensure they are accurate with the desired disks seen in the lsblk command output."
    exit 0
fi

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

mount $ROOT_PART_990 $ROOT_MOUNT_LOCATION_990
check_success "Failed to mount root partition"
echo "Mounting root partition: $ROOT_PART_990 to $ROOT_MOUNT_LOCATION_990"

mkdir $BOOT_MOUNT_LOCATION_990
check_success "Failed to create boot directory"
echo "Creating boot directory: $BOOT_MOUNT_LOCATION_990"

mount $BOOT_PART_990 $BOOT_MOUNT_LOCATION_990
check_success "Failed to mount boot partition"
echo "Mounting boot partition: $BOOT_PART_990 to $BOOT_MOUNT_LOCATION_990"

clear

printf "\e[1;31m~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
printf "     _______.____    ____ .__   __.          ______        _______.\n"
printf "    /       |\   \  /   / |  \ |  |         /  __  \      /       |\n"
printf "   |   (----  \   \/   /  |   \|  |  ______|  |  |  |    |    ---- \n"
printf "    \   \      \_    _/   |  . \`  | |______|  |  |  |     \   \    \n"
printf ".----    |       |  |     |  |\   |        |  \`--'  | .----    |   \n"
printf "|_______/        |__|     |__| \__|         \______/  |_______/    \n"
printf "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\e[0m\n\n"
sleep 1
echo "Installing the Syntax Operating System"
sleep 1
echo ""
echo "(((((((((((((((((((((((((/((((((((/***//////////////////////////////////////////"
echo "(((((((((((((((((((((((((/**(((/*******/////////////////////////////////////////"
echo "((((((((((((((((((((((((((***,,,,,,,,,,,,,,,,*****//////////////////////////////"
echo "((((((((((((((((((/********,,,,,,,,,,,,,,,,,,,,,,**/////////////////////////////"
echo "(((((((((((((((/****,,**,**,,,,,,,,,,,,,,,,,,,,,,,,****/////////////////////////"
echo "((((((((((((/****,,,,,,,,,,,,,,,,*,,,,,,,,,,,,,,,********///////////////////////"
echo "((((((((((**********,,,*/******,,,,*,,,,*,,,,,,,,,,,,**/////////////////////////"
echo "(((((((((********,,,**///(((((((((//**,,,,,,,,,,,,,,,,,,*///////////////////////"
echo "(((((((((******,,,,*/(((((((((((//(((**,,,,,,,,,*,,,,,***,*/////////////////////"
echo "(((((((//****,,,,,,/((((((((((/(//*****,,,,,,,,,,,,,,***/**/////////////////////"
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
echo ",,,,,,,,,,,*********,,,,,,,,,,***,****,,,,,,,******************//***************"
echo ""
sleep 1
echo "Ignorance is the poison, knowledge is the nourish" 
echo ""
sleep 1
clear 
echo "Ignorance is the poison, knowledge is the nourish"
echo "" 
sleep 1
echo  " ___  _   ___ ___ _____ ___    _   ___ "
echo  "| _ \/_\ / __/ __|_   _| _ \  /_\ | _ |"""
echo  "|  _/ _ \ (__\__ \ | | |   / / _ \|  _/"
echo  "|_|/_/ \_\___|___/ |_| |_|_\/_/ \_\_|  "
echo ""
echo "Installing packages to the resulting system."
echo "Applying mirror mystics and re-securing the keyring"

# If you wanted to add your packages:
# Add packages after $SYNSTALL like this "pacstrap /mnt $SYNSTALL firefox mixxx virtualbox some-other-package"

reflector -c "GB" -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist
check_success "Failed to apply mirror mystics"

pacman -Sy			# Update package databases
check_success "Failed to update package databases"

pacman-key --init
pacman-key --populate archlinux
check_success "Failed to secure the keyring"

pacman -Sy			# Update package databases

pacstrap /mnt $SYNSTALL
check_success "Failed to install packages"

echo  "Generating filesystem table with boot information in respect to UUID assignment"

genfstab -U /mnt >> /mnt/etc/fstab
check_success "Failed to generate filesystem table"

echo  "  ____   ___ _____ _____ ___    _   ___ "
echo  " |  _ \ / _ \_   _|  ___|_ _| |   | ____/ ___| "
echo  " | | | | | | || | | |_   | || |   |  _| \___ \ "
echo  " | |_| | |_| || | |  _|  | || |___| |___ ___) |"
echo  " |____/ \___/ |_| |_|   |___|_____|_____|____/ "
echo ""
echo  " Copying the root overlay materials to the result system root directory"
echo ""

ROOT_OVERLAY_DIRECTORY="/root/SYN-OS-V4/root_overlay/*"

cp -R $ROOT_OVERLAY_DIRECTORY $ROOT_MOUNT_LOCATION_990
check_success "Failed to copy root overlay materials"

cp -R /root/syn-stage1.sh $ROOT_MOUNT_LOCATION_990/root/syn-stage1.sh
check_success "Failed to copy stage one script"

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
echo "Congratulations! Stage Zero of the process is complete. To continue building the system, you need to perform the following steps:"
echo ""
echo "1. Mount the root partition ($ROOT_PART_990) to the root directory ($ROOT_MOUNT_LOCATION_990)."
echo "2. Create the boot partition ($BOOT_PART_990) and format it with the appropriate filesystem ($BOOT_FILESYSTEM_990)."
echo "3. Format the root partition ($ROOT_PART_990) with the appropriate filesystem ($ROOT_FILESYSTEM_990)."
echo "4. Mount the boot partition ($BOOT_PART_990) to the boot directory ($BOOT_MOUNT_LOCATION_990)."
echo "5. Generate the filesystem table with boot information in respect to UUID assignment."
echo "6. Install the essential packages to the resulting system using Pacstrap."
echo "7. Apply mirror mystics and re-secure the keyring."
echo "8. Generate cryptographic keys for Pacman and update the package databases."
echo "9. Copy the root overlay materials from $ROOT_OVERLAY_DIRECTORY to the root directory."
echo "10. Complete Stage Zero by arch-chrooting into the new system and executing syn-stage1.sh."
