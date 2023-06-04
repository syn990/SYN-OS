#!/bin/sh
#Assumption - System has internet access and script is being run inside installer without chroot. All detected disks to be erased


DEFAULT_USER990=syntax990
FINAL_HOSTNAME990=SYN-TESTBUILD




# Setting up SYN-ARCHSO keyboard layout in TTY to UK
    loadkeys uk

# Setting up SYN-ARCHSO for NTP
    timedatectl set-ntp true

# Going completley nuclear and erasing disks with GPT partition and root partition
    parted --script /dev/sda mklabel gpt mkpart primary fat32 1Mib 200Mib set 1 boot on
    parted --script /dev/sda mkpart primary ext4 201Mib 100%
        mkfs.vfat -F /dev/sda1
        mkfs.ext4 -F /dev/sda2
            mount /dev/sda2 /mnt
            mkdir /mnt/boot/
            mount /dev/sda1 /mnt/boot
 
    # Update live enviroment with latest packages from /etc/mirrorlist
        # Use reflector and download the best mirrorlist for the live enviroment
            # Tell pacstrap to download all required packages to new install at /mnt
                pacman -Syy
                    #reflector -c "GB" -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist
                    ### Reflector should not be included until SYN-RTOS ISO has been released or else if it's not preempted it will fail at this step.
                    pacstrap /mnt alsa-utils base base-devel dhcpcd dnsmasq dosfstools fakeroot feh gcc git htop iwd kitty kwrite linux linux-firmware lshw lxqt-qtplugin lxrandr nano openbox pcmanfm-qt qutebrowser pulseaudio python-pyalsa ranger reflector rsync spectacle sshfs sudo tint2 vlc xf86-video-vesa xorg-bdftopcf xorg-docs xorg-fonts-100dpi xorg-fonts-75dpi xorg-fonts-encodings xorg-font-util xorg-iceauth xorg-mkfontscale xorg-server xorg-server-common xorg-server-devel xorg-server-xephyr xorg-server-xnest xorg-server-xvfb xorg-sessreg xorg-setxkbmap xorg-smproxy xorg-x11perf xorg-xauth xorg-xbacklight xorg-xcmsdb xorg-xcursorgen xorg-xdpyinfo xorg-xdriinfo xorg-xev xorg-xgamma xorg-xhost xorg-xinit xorg-xinput xorg-xkbcomp xorg-xkbevd xorg-xkbutils xorg-xkill xorg-xlsatoms xorg-xlsclients xorg-xmodmap xorg-xpr xorg-xprop xorg-xrandr xorg-xrdb xorg-xrefresh xorg-xset xorg-xsetroot xorg-xvinfo xorg-xwayland xorg-xwd xorg-xwininfo xorg-xwud xterm zsh terminus-font engrampa

# Generate filesystem table with boot information in respect to UUID assignment 
    genfstab -U /mnt >> /mnt/etc/fstab

# Handover to part 2
    mkdir /mnt/root/
        cp -r /root/SYNSTALL/syn-installer0.sh      /mnt/root/
        cp -r /root/SYNSTALL/syn-installer1.sh      /mnt/root/
   #    cp -r /root/SYNSTALL/somefile.sh            /mnt/root/
            arch-chroot /mnt sh /root/syn-installer1.sh

            
            arch-chroot /mnt DEFAULT_USER990=syntax990
            arch-chroot /mnt FINAL_HOSTNAME990=SYN-TESTBUILD
            arch-chroot /mnt echo Setting username
            arch-chroot /mnt echo Setting hardware Clock
            arch-chroot /mnt echo Setting hostname
            arch-chroot /mnt echo Updating package list using optimized mirror
            arch-chroot /mnt    hwclock --systohc           # Run hwclock to generate /etc/adjtime
            arch-chroot /mnt    pacman -Syy && reflector -c "GB" -f 12 -l 10 -n 12 --save /etc/pacman.d/mirrorlist
            arch-chroot /mnt echo Generating Various System Variables
            arch-chroot /mnt    rm /etc/locale.gen
            arch-chroot /mnt    "touch /etc/locale.gen       && echo 'en_GB.UTF-8 UTF-8'                     >> /etc locale.gen"
            arch-chroot /mnt    locale-gen
            arch-chroot /mnt    "touch /etc/locale.conf      && echo 'LANG=en_GB.UTF-8'                      >> /etc/locale.conf"
            arch-chroot /mnt    "touch /etc/vconsole.conf    && echo 'KEYMAP=uk'                             >> /etc/vconsole.conf"
            arch-chroot /mnt    "touch /etc/hostname         && echo $FINAL_HOSTNAME990                      >> /etc/hostname"
            arch-chroot /mnt    "touch /etc/issue            && echo 'Syntax990 RTOS (arch) \r (\l)'         >> /etc/issue"
            arch-chroot /mnt echo Forced Manual-Editi of sudoer file
            arch-chroot /mnt EDITOR=nano visudo
            arch-chroot /mnt echo Create $DEFAULT_USER990
            arch-chroot /mnt echo Create $DEFAULT_USER990 Home Directory
            arch-chroot /mnt echo Add $DEFAULT_USER990 To Wheel Group And Set Password
            arch-chroot /mnt echo Force Password Reset For $DEFAULT_USER990
            arch-chroot /mnt echo Copy Scripts To $DEFAULT_USER990 Account
            arch-chroot /mnt echo Correct Permissions For $DEFAULT_USER990
            arch-chroot /mnt useradd -m -G wheel -s /bin/zsh $DEFAULT_USER990
            arch-chroot /mnt passwd $DEFAULT_USER990
            arch-chroot /mnt mkdir /home/$DEFAULT_USER990/SYNSTALL
            arch-chroot /mnt mkdir /home/$DEFAULT_USER990/.config
            arch-chroot /mnt mkdir /home/$DEFAULT_USER990/.local
            arch-chroot /mnt cp -r /root/SYNSTALL/scripts                    /home/$DEFAULT_USER990/SYNSTALL
            arch-chroot /mnt cp -r /root/SYNSTALL/DEFAULT_USER/.config       /home/$DEFAULT_USER990/.config
            arch-chroot /mnt cp -r /root/SYNSTALL/DEFAULT_USER/.local        /home/$DEFAULT_USER990/.local
            arch-chroot /mnt cp /root/SYNSTALL/DEFAULT_USER/.xinitrc         /home/$DEFAULT_USER990/.xinitrc
            arch-chroot /mnt cp /root/SYNSTALL/DEFAULT_USER/.zshrc           /home/$DEFAULT_USER990/.zshrc
            arch-chroot /mnt chown    $DEFAULT_USER990:$DEFAULT_USER990    -r      /home/$DEFAULT_USER990
            arch-chroot /mnt echo Enabling DHCP client on boot               && arch-chroot / mnt systemctl enable dhcpcd.service
            arch-chroot /mnt echo Enabling Wireless daemon on boot           && arch-chroot / mnt systemctl enable iwd.service
            arch-chroot /mnt echo BOOT-PARAMATER systemd-boot on boot        && arch-chroot / mnt bootctl --path=/boot install
