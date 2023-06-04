# This script automates the proceedure of deleting the previous work directory
# as well as deleting the previous ISO (This is useful as it builds the ISO in the same DIR, ensuring
# my test VM is simple and always looking at the latest ISO...

rm -R /home/syntax990/SYN-OS/WORKDIR
rm -R /home/syntax990/SYN-OS/*.iso
mkarchiso -v -w WORKDIR -o /home/syntax990/SYN-OS /home/syntax990/SYN-OS/SYN-OS-PROFILE/
