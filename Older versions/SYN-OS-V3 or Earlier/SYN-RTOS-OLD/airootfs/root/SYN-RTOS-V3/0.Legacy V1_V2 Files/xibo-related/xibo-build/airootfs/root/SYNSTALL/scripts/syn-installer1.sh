#!/bin/sh
# Assumption - chroot into /mnt after script 0
#################################################################
# Main script variables                                         #
DEFAULT_USER990=xibo                                       #
FINAL_HOSTNAME990=xibo-testbuild                                        #
#################################################################

echo Setting Username Variable
echo Setting Hostname Variable
echo Setting Hardware Clock
echo Updating Package List Using Optimized Mirror

    hwclock --systohc           # Run hwclock to generate /etc/adjtime
    pacman -Syy && reflector -c "GB" -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist
    
echo Generating /etc/locale.gen with LANG=en_GB.UTF-8

        rm /etc/locale.gen         &&       touch /etc/locale.gen
        echo 'en_GB.UTF-8 UTF-8'   >>             /etc/locale.gen
        
    locale-gen

echo Generating Various System Variables
echo Adding $FINAL_HOSTNAME990 to /etc/hostname

        touch /etc/locale.conf      && echo 'LANG=en_GB.UTF-8' >> /etc/locale.conf
        touch /etc/vconsole.conf    && echo 'KEYMAP=uk' >> /etc/vconsole.conf
        touch /etc/hostname         && echo $FINAL_HOSTNAME990 >> /etc/hostname
        touch /etc/issue            && echo 'XIBO Syntax990 RTOS (arch) \r (\l)' >> /etc/issue

echo Forced Manual-Editi of sudoer file

EDITOR=nano visudo

echo Create $DEFAULT_USER990
echo Create $DEFAULT_USER990 Home Directory
echo Add $DEFAULT_USER990 To Wheel Group And Set Password
echo Force Password Reset For $DEFAULT_USER990
echo Copy Scripts To $DEFAULT_USER990 Account
echo Correct Permissions For $DEFAULT_USER990

    useradd -m -G wheel -s /bin/zsh $DEFAULT_USER990
      passwd $DEFAULT_USER990
        mkdir /home/$DEFAULT_USER990/SYNSTALL
        mkdir /home/$DEFAULT_USER990/.config
        mkdir /home/$DEFAULT_USER990/.local
            cp -r /root/SYNSTALL/scripts                    /home/$DEFAULT_USER990/SYNSTALL
            cp -r /root/SYNSTALL/DEFAULT_USER/.config       /home/$DEFAULT_USER990/.config
            cp -r /root/SYNSTALL/DEFAULT_USER/.local        /home/$DEFAULT_USER990/.local
            cp /root/SYNSTALL/DEFAULT_USER/.xinitrc         /home/$DEFAULT_USER990/.xinitrc
            cp /root/SYNSTALL/DEFAULT_USER/.zshrc           /home/$DEFAULT_USER990/.zshrc
      chown    $DEFAULT_USER990:$DEFAULT_USER990    -r      /home/$DEFAULT_USER990
        
echo Enabling Services:
echo systemctl enable dhcpcd.service    && systemctl enable dhcpcd.service
echo systemctl enable iwd.service       && systemctl enable iwd.service
    
#Install bootloader

#       #SYSLINUX (mbr)
#       mkdir /boot/syslinux/                #create dummy folder
#       syslinux-install_update -i -a -m     #install syslinux to mbr
#       nano /boot/syslinux/syslinux.cfg     #force manual correction of syslinux sda1 parameter
#
#       #systemd-boot (gpt / uefi)
echo Using systemd-boot on /boot/
        bootctl --path=/boot install
        
echo AUR package cramming
git clone https://aur.archlinux.org/veyon.git # Download Veyon Source
git clone https://aur.archlinux.org/snapd.git # Download Snapd Source
                #       ^       #
#                     !!!!                    # HUMAN REQUIRE FOR makepkg -si IN EACH DIR
#sudo systemctl enable --now snapd.socket
#sudo ln -s /var/lib/snapd/snap /snap
#sudo snap install xibo-player

