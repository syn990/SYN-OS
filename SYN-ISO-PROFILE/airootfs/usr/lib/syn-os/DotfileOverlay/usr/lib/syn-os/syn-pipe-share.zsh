#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P I P E - S H A R E
#
#   Labwc pipe menu for the SYN-SHARE file-transfer hub. Server-side items
#   self-toggle (label reflects live systemctl state). Every item needing
#   input (password/IP/path) calls syn-share-prompt.zsh <keyword> — one
#   bare word, nothing for labwc's non-shell command tokenizer to mangle.
#   Popups appear before foot even opens; no terminal-with-a-blinking-
#   cursor for what's really a password prompt.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-SHARE (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

SHARE="/usr/lib/syn-os/syn-share.zsh"
PROMPT="/usr/lib/syn-os/syn-share-prompt.zsh"

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

# toggle_item <unit> <label> <start-keyword> <stop-keyword>
# Both sides go through syn-share-prompt.zsh (one bare keyword per item —
# see the banner above), which now runs everything in the undecorated,
# centered, auto-closing popup instead of a bare foot window.
toggle_item() {
  local unit="$1" label="$2" start_kw="$3" stop_kw="$4"
  local item_label kw
  if svc_active "$unit"; then
    item_label="● ${label} — running (click to stop)"
    kw="$stop_kw"
  else
    item_label="○ Start ${label}"
    kw="$start_kw"
  fi
  local safe_label="$(xml_escape "$item_label")"
  print "      <item label=\"$safe_label\">"
  print "        <action name=\"Execute\"><command>$PROMPT ${kw}</command></action>"
  print "      </item>"
}

print '  <menu id="syn-share-server" label="Server">'
toggle_item rsyncd         "rsync"  srv-start-rsync srv-stop-rsync
toggle_item smb            "Samba"  srv-start-samba srv-stop-samba
toggle_item nfs-server      "NFS"    srv-start-nfs   srv-stop-nfs
toggle_item synshare-httpd "HTTP"   srv-start-http  srv-stop-http
toggle_item synshare-tftpd "TFTP"   srv-start-tftp  srv-stop-tftp
toggle_item synshare-nc    "Netcat" srv-start-nc    srv-stop-nc
print '      <separator/>'
print '      <item label="OTG Quick Share (rsync + HTTP)">'
print "        <action name=\"Execute\"><command>$PROMPT srv-otg-start</command></action>"
print '      </item>'
print '      <item label="Stop ALL services">'
print "        <action name=\"Execute\"><command>$PROMPT srv-stop-all</command></action>"
print '      </item>'
print '  </menu>'

print '  <menu id="syn-share-client" label="Client">'
print '      <item label="Set / Probe Server">'
print "        <action name=\"Execute\"><command>$PROMPT cli-set-server</command></action>"
print '      </item>'
print '      <separator/>'
print '      <item label="rsync: Pull">'
print "        <action name=\"Execute\"><command>$PROMPT cli-rsync-pull</command></action>"
print '      </item>'
print '      <item label="rsync: Push">'
print "        <action name=\"Execute\"><command>$PROMPT cli-rsync-push</command></action>"
print '      </item>'
print '      <item label="Samba: Mount">'
print "        <action name=\"Execute\"><command>$PROMPT cli-smb-mount</command></action>"
print '      </item>'
print '      <item label="Samba: Unmount">'
print "        <action name=\"Execute\"><command>$PROMPT cli-smb-umount</command></action>"
print '      </item>'
print '      <item label="NFS: Mount (direct)">'
print "        <action name=\"Execute\"><command>$PROMPT cli-nfs-mount-direct</command></action>"
print '      </item>'
print '      <item label="NFS: Mount (via SSH tunnel)">'
print "        <action name=\"Execute\"><command>$PROMPT cli-nfs-mount-ssh</command></action>"
print '      </item>'
print '      <item label="NFS: Unmount">'
print "        <action name=\"Execute\"><command>$PROMPT cli-nfs-umount</command></action>"
print '      </item>'
print '      <item label="HTTP: Mirror">'
print "        <action name=\"Execute\"><command>$PROMPT cli-http-mirror</command></action>"
print '      </item>'
print '      <item label="TFTP: Get">'
print "        <action name=\"Execute\"><command>$PROMPT cli-tftp-get</command></action>"
print '      </item>'
print '      <item label="TFTP: Put">'
print "        <action name=\"Execute\"><command>$PROMPT cli-tftp-put</command></action>"
print '      </item>'
print '      <item label="Netcat: Send">'
print "        <action name=\"Execute\"><command>$PROMPT cli-nc-send</command></action>"
print '      </item>'
print '      <item label="SSH: Copy (SCP)">'
print "        <action name=\"Execute\"><command>$PROMPT cli-ssh-copy</command></action>"
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
