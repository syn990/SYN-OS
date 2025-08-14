#!/bin/sh

# =============================================================================
#                                SYN-OS .xinitrc
#                  X Initialization File for SYN-OS Environment
# -----------------------------------------------------------------------------
#   This script sets up the X environment, including resources, keymaps,
#   and window manager for SYN-OS.
#   Author: William Hayward-Holland (Syntax990)
#   License: MIT
# =============================================================================

userresources=$HOME/.Xresources
usermodmap=$HOME/.Xmodmap
sysresources=/etc/X11/xinit/.Xresources
sysmodmap=/etc/X11/xinit/.Xmodmap

# Merge system-wide and user-specific X resources and keymaps
if [ -f $sysresources ]; then
    xrdb -merge $sysresources
fi
if [ -f $sysmodmap ]; then
    xmodmap $sysmodmap
fi
if [ -f "$userresources" ]; then
    xrdb -merge "$userresources"
fi
if [ -f "$usermodmap" ]; then
    xmodmap "$usermodmap"
fi

# Start additional scripts
if [ -d /etc/X11/xinit/xinitrc.d ]; then
  for f in /etc/X11/xinit/xinitrc.d/?*.sh; do
    [ -x "$f" ] && . "$f"
  done
  unset f
fi

# Set keyboard layout to GB and start Openbox with compositing
setxkbmap gb
exec openbox-session & xcompmgr
