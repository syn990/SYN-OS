#!/bin/bash

# Variables
ARCHISO_WORKDIR="/home/syntax990/SYN-OS/WORKDIR"
SYN_ISO_DIR="/home/syntax990/SYN-OS"
SYN_ISO_PROFILE="/home/syntax990/SYN-OS/SYN-OS-V4/SYN-ISO-PROFILE/"
INSTALLER_SCRIPTS_DIR="/home/syntax990/SYN-OS/SYN-OS-V4/SYN-INSTALLER-SCRIPTS"
TARGET_DIR="/home/syntax990/SYN-OS/SYN-OS-V4/SYN-ISO-PROFILE/airootfs/root"

# Check and remove existing WORKDIR
if [ -d "$ARCHISO_WORKDIR" ]; then
    rm -R "$ARCHISO_WORKDIR"
    echo "The directory $ARCHISO_WORKDIR has been deleted."
else
    echo "The directory $ARCHISO_WORKDIR does not exist."
fi

# Check and remove existing ISO files
for iso_file in "$SYN_ISO_DIR"/*.iso; do
    if [ -f "$iso_file" ]; then
        rm "$iso_file"
        echo "The old image $iso_file has been deleted."
    else
        echo "No ISO files found in $SYN_ISO_DIR."
    fi
done

# Move installer scripts to target directory
if [ -d "$INSTALLER_SCRIPTS_DIR" ]; then
    cp -R "$INSTALLER_SCRIPTS_DIR" "$TARGET_DIR"
    echo "Moved $INSTALLER_SCRIPTS_DIR to $TARGET_DIR."
else
    echo "The directory $INSTALLER_SCRIPTS_DIR does not exist."
fi

# Create a new ISO
mkarchiso -v -w "$ARCHISO_WORKDIR" -o "$SYN_ISO_DIR" "$SYN_ISO_PROFILE"

# Remove installer scripts from target directory, regardless of mkarchiso success or failure
if [ -d "${TARGET_DIR}/SYN-INSTALLER-SCRIPTS" ]; then
    rm -R "${TARGET_DIR}/SYN-INSTALLER-SCRIPTS"
    echo "Removed SYN-INSTALLER-SCRIPTS from $TARGET_DIR."
else
    echo "The directory SYN-INSTALLER-SCRIPTS was not found in $TARGET_DIR."
fi
