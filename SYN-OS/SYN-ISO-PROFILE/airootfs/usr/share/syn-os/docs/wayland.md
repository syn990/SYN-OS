# Wayland vs X11: What Changed and Why

For a general explanation of what Wayland and X11 actually are, see [What is Wayland?](./concepts/wayland.md). This page covers what changed specifically in SYN-OS and why.

## The transition

SYN-OS previously ran **Openbox + Tint2** on X11. It has since moved fully to Wayland, with **LabWC** as the session compositor: that's what every current script and config in this repo targets. There is no X11 code path left to fall back to.

## What actually changed under the hood

- **Window manager → compositor:** Openbox (a WM that ran on top of an X server) was replaced by [LabWC](./labwc.md), which is both the compositor and the window manager, so there's no separate X server process, no `xrandr`, no `.xinitrc`.
- **Panel:** Tint2 (X11) was replaced by [Waybar](./waybar.md), which speaks Wayland's `wlr` protocols directly (`wlr/workspaces`, `wlr/window` modules depend on wlroots-based compositors like LabWC).
- **Launcher:** dmenu-style X11 launchers were replaced by `wmenu`, a Wayland-native dmenu equivalent.
- **Session launch:** the `synos` alias in [`.zshrc`](./zsh.md), `dbus-run-session -- env XDG_SESSION_TYPE=wayland labwc`, replaces whatever previously started `startx`/`.xinitrc` with Openbox.
- **Theme continuity:** LabWC deliberately reuses the Openbox theme format (`themerc`, `openbox-3/` theme directories under `usr/share/themes/`), which is why `SYN-OS-RED`, originally an Openbox theme, still works unmodified. This wasn't incidental; it's the reason LabWC was chosen over other wlroots compositors.

## Why this matters if you're modifying SYN-OS

Any documentation, forum post, or personal notes referencing Openbox's `rc.xml`/`menu.xml` semantics, Tint2 config, or `.xinitrc` describe a desktop stack this repo no longer ships. LabWC's `rc.xml` looks similar to Openbox's — same tag names for keybinds/mousebinds — but it's a distinct project with its own protocol support and limitations. Check [LabWC's own documentation](https://github.com/labwc/labwc/wiki) rather than assuming 1:1 Openbox compatibility for anything not already present in this repo's `rc.xml`.
