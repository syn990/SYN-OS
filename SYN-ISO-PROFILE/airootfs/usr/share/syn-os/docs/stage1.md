# The second half of the install

Once your disk is partitioned and the base system is on it, the installer
switches into that new system and finishes setting it up from the
inside.

This is where it sets your locale, hostname, and time zone, creates your
user account with the password you chose, builds the initramfs (with
encryption support baked in, if you turned that on), and installs the
right bootloader for your setup so your machine actually knows how to
start SYN-OS.

It also turns on the networking services you'll need right away, and SSH
too, if you asked for it in the config file.

Every tool SYN-OS ships is already built and ready by this point, nothing
gets compiled during install.

When this finishes, you're done. Reboot, remove the install media, and
log into your new desktop.
