#!/bin/zsh

# SYN-OS
# SYNTAX990
# William Hayward-Holland
# MIT License

# syn-lanbridge.sh

# Create a wireless bridge between a wireless and ethernet port for NAT (Internet Sharing - ArchWiki).
# Assumes manual connection to a wireless network with another computer/switch plugged in via ethernet.

# Define interface variables detected on boot.
WIRELESS_INTERFACE=wlan0
ETHERNET_INTERFACE=eno1
IP_ADDRESS=139.96.30.100/24
BROADCAST_ADDRESS=139.96.30.0/24

# Bring up Ethernet port
ip link set up dev $ETHERNET_INTERFACE
ip addr add $IP_ADDRESS dev $ETHERNET_INTERFACE

# Enable Packet Forwarding in Kernel
sysctl net.ipv4.ip_forward=1

# Ethernet to WiFi - Bridge / Forwarding
iptables -t nat -A POSTROUTING -o $WIRELESS_INTERFACE -j MASQUERADE
iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
iptables -A FORWARD -i $ETHERNET_INTERFACE -o $WIRELESS_INTERFACE -j ACCEPT

# DHCP firewall rules Ethernet to WiFi 
iptables -I INPUT -p udp --dport 67 -i $ETHERNET_INTERFACE -j ACCEPT
iptables -I INPUT -p udp --dport 53 -s $BROADCAST_ADDRESS -j ACCEPT
iptables -I INPUT -p tcp --dport 53 -s $BROADCAST_ADDRESS -j ACCEPT

# Enable DHCP SERVER on ETHERNET
systemctl start dhcpd4@$ETHERNET_INTERFACE.service 
