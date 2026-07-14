#!/bin/bash
set -euo pipefail

units=(rsyncd smb nfs-server synshare-httpd synshare-tftpd synshare-nc)
labels=(rsyncd Samba NFS HTTP TFTP Netcat)

active_count=0
tooltip_lines=()
for i in "${!units[@]}"; do
  if systemctl is-active --quiet "${units[$i]}" 2>/dev/null; then
    tooltip_lines+=("${labels[$i]}: up")
    active_count=$((active_count + 1))
  else
    tooltip_lines+=("${labels[$i]}: down")
  fi
done

tooltip=$(printf '%s\n' "${tooltip_lines[@]}")
class=""
[ "$active_count" -gt 0 ] && class="active"

python3 -c '
import json, sys
count = int(sys.argv[1])
tooltip = sys.argv[2]
cls = sys.argv[3]
text = f"⇄ {count}" if count > 0 else ""
print(json.dumps({"text": text, "tooltip": tooltip, "class": cls}))
' "$active_count" "$tooltip" "$class"
