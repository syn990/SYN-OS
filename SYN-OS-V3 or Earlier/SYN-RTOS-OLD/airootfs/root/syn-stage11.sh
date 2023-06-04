# Remove and create /etc/locale.gen
rm /etc/locale.gen
touch /etc/locale.gen
echo $LOC_GEN >> /etc/locale.gen

# Generate locales
locale-gen

# Create and configure /etc/locale.conf
touch /etc/locale.conf
echo $LOC_CONF >> /etc/locale.conf

# Create and configure /etc/hostname
touch /etc/hostname
echo $HNAME >> /etc/hostname

# Set time zone
ln -sf /usr/share/zoneinfo/$ZONE /etc/localtime

# Edit sudoers file
EDITOR=nano visudo

# Create new user
useradd -m -G wheel -s $SHELL $USER
passwd $USER
chown $USER:$USER -R /home/$USER

# Enable systemd services
systemctl enable dhcpcd.service
systemctl enable iwd.service

# Install bootloader
bootctl --path=/boot install

echo "done"



