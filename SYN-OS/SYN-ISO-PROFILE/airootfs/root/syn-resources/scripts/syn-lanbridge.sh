#!/bin/zsh

# =============================================================================
#                                 SYN-OS lanbridge
#       Script to Create a Wireless Bridge with Ethernet for NAT Sharing
# -----------------------------------------------------------------------------
#   This script enables Internet sharing from a wireless to an ethernet port,
#   creating a NAT bridge as outlined on the ArchWiki.
#   Author: William Hayward-Holland (Syntax990)
#   License: MIT
# =============================================================================

# Define network interface variables
WIRELESS_INTERFACE=wlan0
ETHERNET_INTERFACE=eno1
IP_ADDRESS=139.96.30.100/24
BROADCAST_ADDRESS=139.96.30.0/24

# Bring up Ethernet port
ip link set up dev $ETHERNET_INTERFACE
ip addr add $IP_ADDRESS dev $ETHERNET_INTERFACE

# Enable packet forwarding in the kernel
sysctl net.ipv4.ip_forward=1

# Configure NAT forwarding between Ethernet and WiFi
iptables -t nat -A POSTROUTING -o $WIRELESS_INTERFACE -j MASQUERADE
iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
iptables -A FORWARD -i $ETHERNET_INTERFACE -o $WIRELESS_INTERFACE -j ACCEPT

# Set up DHCP firewall rules for Ethernet to WiFi
iptables -I INPUT -p udp --dport 67 -i $ETHERNET_INTERFACE -j ACCEPT
iptables -I INPUT -p udp --dport 53 -s $BROADCAST_ADDRESS -j ACCEPT
iptables -I INPUT -p tcp --dport 53 -s $BROADCAST_ADDRESS -j ACCEPT

# Start DHCP server on Ethernet interface
systemctl start dhcpd4@$ETHERNET_INTERFACE.service
