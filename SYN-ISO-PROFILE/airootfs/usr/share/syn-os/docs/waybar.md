# The top bar

The bar across the top of the screen is Waybar. It starts automatically
with the desktop and shows a running-window list on the left, the current
window's title in the middle, and a row of status icons on the right.

![Waybar close-up, full bar width](./screenshots/waybar-closeup.png)
*Placeholder, the full bar at default height.*

## What's on it

Left side: a small SYN-OS glyph that click-opens the app launcher, and a
list of your currently open windows. Click one to switch to it, middle
click to close it.

Middle: the title of whatever window is focused right now.

Right side, left to right:

- **Recording indicator.** Only shows up while a screen recording is
  running. Click it to stop. See [Screenshots & recording](./tools/screenshot-and-recording.md).
- **SYN-SHARE.** Shows how many file-sharing services are currently on.
  Click for a quick menu to start, stop, or check them. See [File sharing](./tools/syn-share.md).
- **VPN.** Shows up only when a WireGuard connection is active, otherwise
  it's not there at all.
- **Network.** Wi-Fi name and speed, or a disconnected icon. Click it to
  open the Wi-Fi picker. See [Wi-Fi](./tools/wifi.md).
- **Backlight.** Screen brightness. Scroll up or down to change it.
- **Volume.** Click to open the audio mixer, middle-click to mute, scroll
  to adjust.
- **CPU, memory, and temperature.** Each one turns yellow, then red, as
  usage climbs. Click any of them to open the system monitor straight to
  that view. See [System monitor & logs](./tools/syn-sysmon.md).
- **Disk usage.** Turns yellow past 75% full, red past 90%. Click it to
  open the file manager at the root of the drive.
- **Battery.** Blinks when critically low and not charging.
- **Clock.**
- **Power button.** Click for Lock, Log Out, Reboot, or Power Off.

## Look

Dark, matching whatever theme is active. Switching themes redraws the bar
automatically, no restart needed. See [The theme system](./theming/theme-engine.md)
for how that works.

## Changing it

The bar's layout lives in `~/.config/waybar/config.jsonc`, its look in
`~/.config/waybar/style.css`. You can edit either by hand, but a theme
switch will overwrite `style.css` with that theme's version, and a fresh
install or rebuild will overwrite both from this repo's own copy. If
you're changing it for good, edit the copy in `DotfileOverlay/` (see [How your settings are set up](./dotfile-overlay.md))
so the change survives.
