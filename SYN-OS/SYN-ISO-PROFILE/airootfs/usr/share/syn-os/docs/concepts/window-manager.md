# What is a Window Manager?

Three terms get conflated a lot: **desktop environment**, **window manager**, and **compositor**. They're related but distinct layers.

## Desktop environment (DE)

A full, integrated suite: window management, a panel/taskbar, a settings app, a file manager, notification daemon, and usually a consistent theme tying it all together. GNOME, KDE Plasma, XFCE. Installing a DE gives you all of these as one coordinated package, with one team deciding how they interoperate.

SYN-OS does **not** ship a DE. It ships the individual pieces, chosen and wired together deliberately: [LabWC](../labwc.md) (window management), [Waybar](../waybar.md) (panel), `wmenu` (launcher), [syn-filemanager](../tools/syn-filemanager.md) (file manager), and `mako` ([notifications](../tools/notifications.md)) — each an independently-maintained project.

## Window manager (WM)

The piece that decides how windows are arranged, moved, resized, focused, and decorated (title bars, borders). A WM doesn't necessarily provide a panel, a launcher, or a file manager — those are separate programs it happens to run alongside. Classic X11 WMs: Openbox, i3, dwm.

## Compositor

On Wayland, the compositor's job absorbs what used to be split across the X server and the window manager: it owns the display output, handles input, and composites (renders and layers) every application's window buffer into the final image shown on screen. There is no separate "display server" process the way X11 had one.

This is why [LabWC](../labwc.md) is described as a "compositor" rather than just a "window manager": it's doing both jobs. See [What is Wayland?](./wayland.md) for why Wayland folded these responsibilities together.

## Where SYN-OS's pieces fit

| Layer | X11 equivalent | SYN-OS |
|---|---|---|
| Compositor / display | X server | LabWC |
| Window management | Openbox | LabWC (same process) |
| Panel | Tint2 | Waybar |
| Launcher | dmenu | wmenu |
| Wallpaper | feh --bg-fill | swaybg |
| Notifications | (varies) | mako |

No single project here calls itself a "desktop environment." SYN-OS's desktop is these pieces, deployed together by [the dotfile overlay](../dotfile-overlay.md) and started by one alias, but each piece remains independently swappable.
