#!/bin/bash

# This script retrieves and continuously outputs the IP addresses and
# interface names for all network interfaces on a system running SYN-OS.

# Display a header message to indicate the start of the output
echo "Interface IP Addresses:"

# Start an infinite loop to continuously output the IP addresses
while true
do
    # Retrieve a list of all network interfaces
    interfaces=$(ip -o link show | awk -F': ' '{print $2}')

    # Loop through each interface and retrieve its IP address(es)
    for interface in $interfaces
    do
        # Retrieve the IP addresses for the interface
        ips=$(ip addr show $interface | grep -oP '\d+\.\d+\.\d+\.\d+' | tr '\n' ' ')

        # Format the output with color and spacing
        echo -e "\e[1m$interface:\e[0m\t$ips"
    done

    # Add a separator line between each iteration for readability
    echo "----------------------------------------"

    # Wait for one second before running the loop again
    sleep 1
done
