#!/usr/bin/env bash
IF=$(ip -o link show up | awk -F': ' '$2 != "lo" {print $2}' | head -n1)
[ -n "$IF" ] || { printf -- "-- --\n"; exit 0; }

RX1=$(cat "/sys/class/net/$IF/statistics/rx_bytes")
TX1=$(cat "/sys/class/net/$IF/statistics/tx_bytes")
sleep 1
RX2=$(cat "/sys/class/net/$IF/statistics/rx_bytes")
TX2=$(cat "/sys/class/net/$IF/statistics/tx_bytes")
RX=$(( (RX2 - RX1) / 1024 ))
TX=$(( (TX2 - TX1) / 1024 ))
printf "%dKB %dKB\n" "$TX" "$RX"