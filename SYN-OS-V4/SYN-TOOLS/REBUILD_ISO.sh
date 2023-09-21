#!/bin/bash

# Variables
WORKDIR="/home/syntax990/SYN-OS/WORKDIR"
ISODIR="/home/syntax990/SYN-OS"
ISOPROFILE="/home/syntax990/SYN-OS/SYN-OS-V4/SYN-ISO-PROFILE/"

# Check and remove existing WORKDIR
if [ -d "$WORKDIR" ]; then
    rm -R "$WORKDIR"
    echo "The directory $WORKDIR has been deleted."
else
    echo "The directory $WORKDIR does not exist."
fi

# Check and remove existing ISO files
for iso_file in "$ISODIR"/*.iso; do
    if [ -f "$iso_file" ]; then
        rm "$iso_file"
        echo "The file $iso_file has been deleted."
    else
        echo "No ISO files found in $ISODIR."
    fi
done

# Create a new ISO
mkarchiso -v -w "$WORKDIR" -o "$ISODIR" "$ISOPROFILE"
