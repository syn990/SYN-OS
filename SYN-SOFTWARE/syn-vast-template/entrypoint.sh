#!/bin/sh
# ------------------------------------------------------------------------------
#                    S Y N - V A S T - E N T R Y P O I N T
#
#   Container ENTRYPOINT — replaces the manual `synos` TTY alias
#   (dbus-run-session -- env XDG_SESSION_TYPE=wayland labwc) with an
#   automatic headless launch. WLR_BACKENDS=headless is wlroots' own
#   documented no-GPU-output mode: creates a virtual output a WebRTC
#   streamer (Selkies) can attach to and capture, same
#   ext-image-copy-capture-v1 mechanism syn-relay's screen-host role
#   already uses on a real display.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-VAST-TEMPLATE (Cloud container)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

export WLR_BACKENDS=headless
export WLR_LIBINPUT_NO_DEVICES=1
export XDG_SESSION_TYPE=wayland
export XDG_RUNTIME_DIR="/tmp/xdg-runtime-$(id -u)"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

exec dbus-run-session -- labwc
