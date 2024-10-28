#Basic script to use/modify if DHCP fails and you need a static IP
ip_address=139.96.30.101/24
ip_route=139.96.30.100
ethernet_interface=eno1

ip add add $ipaddress dev $ethernet_interface
ip link set up dev $ethernet_interface
ip route add default via $ip_route dev $ethernet_interface

systemctl start systemd-resolved.service
