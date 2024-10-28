#ASSUMTION
#You wish to create wireless bridge between wireless and ethernet port as a NAT. #InternetSharingArchWiki!
#Manually joined wireless network and there is another computer/switch plugged in via ethernet.
#
# Change these variables to suit interfaces that were detected on boot.
WIRELESS_INTERFACE990=wlan0
ETHERNET_INTERFACE990=eno1


#Bring up Ethernet port
ip link set up dev $ETHERNET_INTERFACE990
ip addr add 139.96.30.100/24 dev $ETHERNET_INTERFACE990

#Enable Packet Forwarding in Kernel
sysctl net.ipv4.ip_forward=1

#Ethernet to WiFi - Bridge / Forwarding
iptables -t nat -A POSTROUTING -o $WIRELESS_INTERFACE990 -j MASQUERADE
iptables -A FORWARD -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT
iptables -A FORWARD -i $ETHERNET_INTERFACE990 -o $WIRELESS_INTERFACE990 -j ACCEPT

#DHCP firewall rules Ethernet to WiFi 
iptables -I INPUT -p udp --dport 67 -i net0 -j ACCEPT
iptables -I INPUT -p udp --dport 53 -s 139.96.30.0/24 -j ACCEPT
iptables -I INPUT -p tcp --dport 53 -s 139.96.30.0/24 -j ACCEPT

#Enable DHCP SERVER on ETHERNET
systemctl start dhcpd4@$ETHERNET_INTERFACE990.service 
