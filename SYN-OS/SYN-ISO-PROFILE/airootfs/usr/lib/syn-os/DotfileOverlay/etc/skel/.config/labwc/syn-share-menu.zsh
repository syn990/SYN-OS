#!/usr/bin/env zsh
# SYN-SHARE Menu: labwc pipe-menu for the file-transfer hub. Server-side
# items self-toggle (label reflects live systemctl state); passwords/paths
# are collected inline via wmenu before syn-share.zsh is invoked, same
# pattern as SYN-GRAPHMAP's "Custom (enter a depth)" entry.
set -euo pipefail

SHARE="/usr/lib/syn-os/syn-share.zsh"
SERVER_FILE="$HOME/.config/syn-os/syn-share-server"

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'

xml_escape() {
  print -r -- "$1" | sed \
    -e 's/&/\&amp;/g' \
    -e 's/"/\&quot;/g' \
    -e "s/'/\&apos;/g" \
    -e 's/</\&lt;/g' \
    -e 's/>/\&gt;/g'
}

svc_active() { systemctl is-active --quiet "$1" 2>/dev/null; }

saved_ip=""
[[ -f "$SERVER_FILE" ]] && saved_ip="$(<"$SERVER_FILE")"

# toggle_item <unit> <label> <start-cmd> <stop-cmd>
toggle_item() {
  local unit="$1" label="$2" start_cmd="$3" stop_cmd="$4"
  local item_label cmd
  if svc_active "$unit"; then
    item_label="● ${label} — running (click to stop)"
    cmd="$stop_cmd"
  else
    item_label="○ Start ${label}"
    cmd="$start_cmd"
  fi
  local safe_label="$(xml_escape "$item_label")"
  print "      <item label=\"$safe_label\">"
  print "        <action name=\"Execute\"><command>foot -e zsh -c '${cmd}; exec zsh'</command></action>"
  print "      </item>"
}

print '  <menu id="syn-share-server" label="Server">'
toggle_item rsyncd         "rsync"  "pass=\$(print \"\" | wmenu -p \"rsync password:\"); zsh $SHARE srv-start-rsync \"\$pass\"" "zsh $SHARE srv-stop-rsync"
toggle_item smb            "Samba"  "pass=\$(print \"\" | wmenu -p \"Samba password:\"); zsh $SHARE srv-start-samba \"\$pass\"" "zsh $SHARE srv-stop-samba"
toggle_item nfs-server      "NFS"    "zsh $SHARE srv-start-nfs" "zsh $SHARE srv-stop-nfs"
toggle_item synshare-httpd "HTTP"   "zsh $SHARE srv-start-http" "zsh $SHARE srv-stop-http"
toggle_item synshare-tftpd "TFTP"   "zsh $SHARE srv-start-tftp" "zsh $SHARE srv-stop-tftp"
toggle_item synshare-nc    "Netcat" "zsh $SHARE srv-start-nc" "zsh $SHARE srv-stop-nc"
print '      <separator/>'
print '      <item label="OTG Quick Share (rsync + HTTP)">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'pass=\$(print \"\" | wmenu -p \"rsync password:\"); zsh $SHARE srv-otg-start \"\$pass\"; exec zsh'</command></action>"
print '      </item>'
print '      <item label="Stop ALL services">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'confirm=\$(print \"\" | wmenu -p \"Type yes to stop ALL:\"); [ \"\$confirm\" = yes ] &amp;&amp; zsh $SHARE srv-stop-all; exec zsh'</command></action>"
print '      </item>'
print '  </menu>'

print '  <menu id="syn-share-client" label="Client">'
print '      <item label="Set / Probe Server">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP or hostname:\"); zsh $SHARE cli-set-server \"\$ip\"; exec zsh'</command></action>"
print '      </item>'
print '      <separator/>'
print '      <item label="rsync: Pull">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); pass=\$(print \"\" | wmenu -p \"rsync password:\"); zsh $SHARE cli-rsync-pull \"\$ip\" \"\$pass\"; exec zsh'</command></action>"
print '      </item>'
print '      <item label="rsync: Push">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); pass=\$(print \"\" | wmenu -p \"rsync password:\"); src=\$(print \"\" | wmenu -p \"Local path:\"); zsh $SHARE cli-rsync-push \"\$ip\" \"\$pass\" \"\$src\"; exec zsh'</command></action>"
print '      </item>'
print '      <item label="Samba: Mount">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); pass=\$(print \"\" | wmenu -p \"Samba password:\"); zsh $SHARE cli-smb-mount \"\$ip\" \"\$pass\"; exec zsh'</command></action>"
print '      </item>'
print '      <item label="Samba: Unmount">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'zsh $SHARE cli-smb-umount; exec zsh'</command></action>"
print '      </item>'
print '      <item label="NFS: Mount (direct)">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); zsh $SHARE cli-nfs-mount \"\$ip\" 0; exec zsh'</command></action>"
print '      </item>'
print '      <item label="NFS: Mount (via SSH tunnel)">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); zsh $SHARE cli-nfs-mount \"\$ip\" 1; exec zsh'</command></action>"
print '      </item>'
print '      <item label="NFS: Unmount">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'zsh $SHARE cli-nfs-umount; exec zsh'</command></action>"
print '      </item>'
print '      <item label="HTTP: Mirror">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); zsh $SHARE cli-http-mirror \"\$ip\"; exec zsh'</command></action>"
print '      </item>'
print '      <item label="TFTP: Get">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); f=\$(print \"\" | wmenu -p \"Remote filename:\"); zsh $SHARE cli-tftp-get \"\$ip\" \"\$f\"; exec zsh'</command></action>"
print '      </item>'
print '      <item label="TFTP: Put">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); f=\$(print \"\" | wmenu -p \"Local file:\"); zsh $SHARE cli-tftp-put \"\$ip\" \"\$f\"; exec zsh'</command></action>"
print '      </item>'
print '      <item label="Netcat: Send">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); src=\$(print \"\" | wmenu -p \"Local path:\"); zsh $SHARE cli-nc-send \"\$ip\" \"\$src\"; exec zsh'</command></action>"
print '      </item>'
print '      <item label="SSH: Copy (SCP)">'
print "        <action name=\"Execute\"><command>foot -e zsh -c 'ip=\$(print \"$saved_ip\" | wmenu -p \"Server IP:\"); src=\$(print \"\" | wmenu -p \"Local path:\"); dst=\$(print \"\" | wmenu -p \"Remote destination:\"); zsh $SHARE cli-ssh-copy \"\$ip\" \"\$src\" \"\$dst\"; exec zsh'</command></action>"
print '      </item>'
print '  </menu>'

print '  <separator/>'
print '  <item label="Service Status">'
print "    <action name=\"Execute\"><command>foot -e zsh -c 'zsh $SHARE status; echo; read -k1 -s \"?Press any key\"'</command></action>"
print '  </item>'
print '  <item label="View Log">'
print '    <action name="Execute"><command>foot -e less /var/log/syn-share.log</command></action>'
print '  </item>'

print '</openbox_pipe_menu>'
