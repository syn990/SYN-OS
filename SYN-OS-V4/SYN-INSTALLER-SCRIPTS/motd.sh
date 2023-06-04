#!/bin/bash

printf "\n\e[1;32mWelcome to \e[1mSYN-OS\e[0;32m. This robust and advanced operating system marks the evolution of building your own operating system, based on \e[1mArch Linux\e[0;32m.\e[0m\n\n"

printf "For installation enquiries, contact the creator, \e[1;33mSyntax990\e[0m at \e[1;34mwilliam@npc.syntax990.com\e[0m. Build materials can be obtained from our Github repository: \e[1;34mhttps://github.com/syn990/syn-rtos.git\e[0m\n\n"

printf "01010011 01011001 01001110 00101101 01001111 01010011"
printf "SYN-OS: The Syntax Operating System"
printf ""
printf "\e[1;31m~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n"
printf "     _______.____    ____ .__   __.          ______        _______.\n"
printf "    /       |\   \  /   / |  \ |  |         /  __  \      /       |\n"
printf "   |   (----  \   \/   /  |   \|  |  ______|  |  |  |    |    ---- \n"
printf "    \   \      \_    _/   |  . \`  | |______|  |  |  |     \   \    \n"
printf ".----    |       |  |     |  |\   |        |  \`--'  | .----    |   \n"
printf "|_______/        |__|     |__| \__|         \______/  |_______/    \n"
printf "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\e[0m\n\n"

printf "In your \e[1;33m/root/\e[0m directory, you will find two significant scripts that require your attention:\n\n"

printf "\e[1;33mSYN-INSTALLER-MAIN.sh\e[0m: This script prepares your \e[1mfilesystem\e[0m on the storage medium using your live host system. It installs the \e[1mpackages\e[0m specified within the script variables. Prior to execution, carefully read and adjust the script as necessary. Lack of discretion may lead to \e[1mdata loss\e[0m.\n\n"

printf "\e[1;33msyn-stage1.sh\e[0m: Once you verify there are no errors or unexpected glitches in the \e[1;33mSYN-INSTALLER-MAIN.sh\e[0m script, \e[1;33msyn-stage1.sh\e[0m should be run within the \e[1mchroot\e[0m environment. This script is automatically replicated to your environment.\n\n"

printf "Execution of these scripts demands serious consideration of their functions. Failure to comprehend their impact may lead to \e[1mirreversible consequences\e[0m.\n\n"

printf "Remember, \e[1msecurity\e[0m and thorough understanding are pivotal to the successful operation of \e[1mSYN-OS\e[0m.\n\n"

printf "Your journey with \e[1mSYN-OS\e[0m begins here. Proceed with the commands as follows:\n\n"

printf "systemctl start dhcpcd.serivce"
printf "Otherwise complete the network configuration setup yourself" 
printf ""
printf "\e[1;34msh SYN-INSTALLER-MAIN.sh\n"
