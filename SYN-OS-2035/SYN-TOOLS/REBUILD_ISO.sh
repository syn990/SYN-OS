#!/bin/bash

# This script is used to create a new ISO image for SYN-OS by performing a series of operations.
# It sets up the necessary variables, checks for existing directories and files, moves installer scripts,
# creates a new ISO using mkarchiso, and removes installer scripts from the target directory.

# Variables
ARCHISO_WORKDIR="/home/syntax990/Github-Projects/SYN-OS/WORKDIR"                                # The working directory for mkarchiso.
SYN_ISO_DIR="/home/syntax990/Github-Projects/SYN-OS"                                            # The directory where the SYN-OS ISO files are located.
SYN_ISO_PROFILE="/home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/"             # The profile directory for mkarchiso.
INSTALLER_SCRIPTS_DIR="/home/syntax990/SYN-OS/SYN-OS-2035/SYN-INSTALLER-SCRIPTS"  # The directory containing the installer scripts.
TARGET_DIR="/home/syntax990/Github-Projects/SYN-OS/SYN-OS-2035/SYN-ISO-PROFILE/airootfs/root"     # The target directory for moving installer scripts.

# Check and remove existing WORKDIR
# Check if the ARCHISO_WORKDIR directory exists.
# If it exists, remove it and display a message.
# If it doesn't exist, display a message indicating that it doesn't exist.
[ -d "$ARCHISO_WORKDIR" ] && { rm -R "$ARCHISO_WORKDIR"; echo "The directory $ARCHISO_WORKDIR has been deleted."; } || echo "The directory $ARCHISO_WORKDIR does not exist."

# Check and remove existing ISO files
# Iterate over all the .iso files in the SYN_ISO_DIR directory.
# If a file is found, remove it and display a message.
# If no files are found, display a message indicating that no ISO files were found.
found_iso=false
for iso_file in "$SYN_ISO_DIR"/*.iso; do
    [ -f "$iso_file" ] && { rm "$iso_file"; echo "The old image $iso_file has been deleted."; found_iso=true; }
done
[ "$found_iso" = false ] && echo "No ISO files found in $SYN_ISO_DIR."

# Move installer scripts to target directory
# Check if the INSTALLER_SCRIPTS_DIR directory exists.
# If it exists, copy its contents to the TARGET_DIR directory and display a message.
# If it doesn't exist, display a message indicating that it doesn't exist.
#[ -d "$INSTALLER_SCRIPTS_DIR" ] && { cp -R "$INSTALLER_SCRIPTS_DIR" "$TARGET_DIR"; echo "Moved $INSTALLER_SCRIPTS_DIR to $TARGET_DIR."; } || echo "The directory $INSTALLER_SCRIPTS_DIR does not exist."

# Create a new ISO
# Use mkarchiso to create a new ISO image.
# The -v option enables verbose output.
# The -w option specifies the working directory.
# The -o option specifies the output directory.
# The SYN_ISO_PROFILE variable specifies the profile directory.
mkarchiso -v -w "$ARCHISO_WORKDIR" -o "$SYN_ISO_DIR" "$SYN_ISO_PROFILE"

# Remove installer scripts from target directory, regardless of mkarchiso success or failure
# Check if the SYN-INSTALLER-SCRIPTS directory exists in the TARGET_DIR directory.
# If it exists, remove it and display a message.
# If it doesn't exist, display a message indicating that it wasn't found.
        #[ -d "${TARGET_DIR}/SYN-INSTALLER-SCRIPTS" ] && { rm -R "${TARGET_DIR}/SYN-INSTALLER-SCRIPTS"; echo "Removed SYN-INSTALLER-SCRIPTS from $TARGET_DIR."; } || echo "The directory SYN-INSTALLER-SCRIPTS was not found in $TARGET_DIR."
