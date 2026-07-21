# The window manager

SYN-OS runs [LabWC](https://github.com/labwc/labwc), a lightweight Wayland
window manager. See [Why Wayland, not X11](./wayland.md) for why. LabWC
uses the same config format as the older Openbox, so if you've used
Openbox before, this will look familiar.

## Keyboard shortcuts

| Key | Does |
|---|---|
| `Super+Space` | Open the main menu |
| `Super+Escape` | Reload the desktop config |
| `Super+l` | Lock the screen |
| `Super+Shift+e` | Log out |
| `Super+Return` | Open a terminal |
| `Super+a` | Open the app launcher |
| `Super+e` | Open the file manager |
| `Super+q` | Close the focused window |
| `Alt+Tab` / `Alt+Shift+Tab` | Switch to the next / previous window |
| `Super+Tab` | Maximize or restore the window |
| `Super+Shift+` arrow keys | Snap a window to a screen edge |
| `Ctrl+Alt+` left/right | Switch desktop |
| `Shift+Alt+` left/right | Move the window to the next desktop |
| `Super+1` through `Super+4` | Jump to desktop 1 to 4 |
| `Super+p` | Screenshot a selected area |
| `Super+Shift+p` | Start or stop recording the screen |

More on screenshots and recording: [Screenshots & recording](./tools/screenshot-and-recording.md).

Mouse: hold `Alt` and drag with the left button to move a window, right
button to resize it, middle button to send it behind everything else.

There are four virtual desktops, meant loosely as Terminal, Web, Media,
and External, though you can use them however you like.

## The main menu (`Super+Space`)

![The root menu open over the desktop](./screenshots/menu-xml-root-open.png)
*Placeholder, the menu open over the desktop.*

**Applications**: a terminal, the file manager, and "All Applications" for
everything else installed on the system.

**Capture**: screenshot the full screen or a selected area, and start or
stop a screen recording.

**SYN-OS Tools**: every tool this project builds, in one place. The
Wi-Fi picker, volume mixer, system monitor, system logs, file sharing,
encryption, directory mapping, the ISO builder, and this same
documentation.

**Preferences**: audio, [display](./tools/display.md), and theme
settings, plus toggles for BlackArch tools and system services.

**System**: lock the screen, kill a stuck app, edit a config file by
hand, or restart the desktop bar.

**Power**: reboot or power off.

## Startup

When you log in, SYN-OS restores any monitors you'd previously turned off,
applies your active theme, and starts the desktop bar and notifications.
None of this needs you to do anything, it just happens.

## Changing settings by hand

The window manager's settings live in a few plain text files under
`~/.config/labwc/`. You can edit them directly and press `Super+Escape` to
reload without logging out. Some changes, like environment variables,
only take effect after you log back in.

If you're changing SYN-OS itself rather than just your own machine, edit
the copy under `DotfileOverlay/` in the repo instead, so the change
survives a rebuild. See [How your settings are set up](./dotfile-overlay.md).
