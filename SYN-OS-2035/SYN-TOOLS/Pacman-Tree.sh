#!/bin/bash
echo Installed Packages as of $(date): 
pacman -Qq

echo Here is a coprehensive dependancy tree:
for package in $(pacman -Qq); do
    echo "Package: $package"
    echo "----------"
    pactree -r $package
    echo "----------"
done
