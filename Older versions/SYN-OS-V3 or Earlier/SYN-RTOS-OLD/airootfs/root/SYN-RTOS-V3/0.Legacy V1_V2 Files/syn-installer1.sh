#!/bin/sh
# Assumption - chroot into /mnt after script 0

# Main script variables                                         
DEFAULT_USER_990=syntax990
FINAL_HOSTNAME_990=SYN-TESTBUILD
LOCALE_GEN_990="en_GB.UTF-8 UTF-8"
LOCALE_CONF_990="LANG=en_GB.UTF-8"
KEYMAP_990="KEYMAP=uk"
ISSUE_NAME_990="SYN-RTOS-V3 - Syntax Real-Time Operating System"
SHELL_CHOICE_990=/bin/zsh

echo Setting username
echo Setting hostname
echo Setting hardware Clock
echo Updating package list using optimized mirror

    hwclock --systohc           # Run hwclock to generate /etc/adjtime
    pacman -Syy && reflector -c "GB" -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist
    
echo Generating Various System Variables

    rm /etc/locale.gen
    touch /etc/locale.gen       && echo $LOCALE_GEN_990                         >> /etc/locale.gen
    locale-gen
    touch /etc/locale.conf      && echo $LOCALE_CONF_990                        >> /etc/locale.conf
    touch /etc/vconsole.conf    && echo $KEYMAP_990                             >> /etc/vconsole.conf
    touch /etc/hostname         && echo $FINAL_HOSTNAME_990                     >> /etc/hostname
    touch /etc/issue            && echo $ISSUE_NAME_990                         >> /etc/issue
    ln -sf                      /usr/share/zoneinfo/GB                          /etc/localtime

echo Forced Manual-Edit of sudoer file

EDITOR=nano visudo

echo Create $DEFAULT_USER_990
echo Create $DEFAULT_USER_990 Home Directory
echo Add $DEFAULT_USER_990 To Wheel Group And Set Password
echo Force Password Reset For $DEFAULT_USER_990
echo Copy Scripts To $DEFAULT_USER_990 Account
echo Correct Permissions For $DEFAULT_USER_990

# Create the user based on the variables, assign them a default shell then take ownership of home directory and files.
    useradd -m -G wheel -s $SHELL_CHOICE_990 $DEFAULT_USER_990
      passwd $DEFAULT_USER_990
      chown    $DEFAULT_USER_990:$DEFAULT_USER_990 -r /home/$DEFAULT_USER_990
        
        
# Enable systemd services for DHCP, WiFi and setup the bootloader
echo Enabling DHCP client on boot               && systemctl enable dhcpcd.service
echo Enabling Wireless daemon on boot           && systemctl enable iwd.service    
echo BOOT-PARAMATER systemd-boot on boot        && bootctl --path=/boot install
