#!/bin/bash
set -e

# This script is designed to automate the process of creating a custom Message of the Day (MOTD) for an Arch Linux live environment (archiso).
# It assumes that the necessary dependencies for building an archiso have already been installed on the system.

# First, we define some variables. The PROFILE_PATH variable is the path to the directory where your custom archiso profile is located.
# The MOTD_SCRIPT variable is the name of the script that will display the MOTD.
PROFILE_PATH="/home/syntax990/SYN-OS/SYN-OS-V4/SYN-ISO-PROFILE"  # Path to the custom archiso profile directory
MOTD_SCRIPT="motd.sh"  # Name of the script that displays the MOTD

# We use the 'cd' command to change the current directory to the directory where the custom archiso profile is located.
cd "$PROFILE_PATH"

# We check if the necessary directories exist within the profile directory. If they don't exist, we create them.
# The 'airootfs' directory corresponds to the root filesystem of the live environment, and this is where we will place our custom scripts.
# The 'airootfs/root' directory corresponds to the /root directory in the live environment, which is the home directory of the root user.
# The 'airootfs/etc/profile.d' directory contains scripts that are executed whenever a user logs in to a shell session.
[ -d "airootfs" ] || mkdir airootfs
[ -d "airootfs/root" ] || mkdir -p airootfs/root
[ -d "airootfs/etc/profile.d" ] || mkdir -p airootfs/etc/profile.d

# We create the MOTD script in the 'airootfs/root' directory.
# We use a 'here document' (<< 'EOF') to write multiple lines of text to the file.
# The 'cat' command is used to concatenate and display file contents. In this case, we're directing the output into the MOTD script file.
# Replace "Your MOTD text goes here" with your actual MOTD text, including the ANSI escape sequences for color and formatting.
cat << 'EOF' > airootfs/root/$MOTD_SCRIPT
#!/bin/bash
echo -e "Your MOTD text goes here"
EOF

# We use the 'chmod' command to make the MOTD script executable. This is necessary for the script to be run.
chmod +x airootfs/root/$MOTD_SCRIPT

# We create a new script in the 'airootfs/etc/profile.d' directory. This script will call the MOTD script when a user logs in.
# Again, we use a 'here document' to write multiple lines of text to the file.
cat << 'EOF' > airootfs/etc/profile.d/99-motd.sh
#!/bin/bash
/root/motd.sh
EOF

# We make the new script executable.
chmod +x airootfs/etc/profile.d/99-motd.sh
