ip add add 139.96.30.101/24 dev eno1
ip link set up dev eno1
ip route add default via 139.96.30.100 dev eno1
systemctl start systemd-resolved.service
