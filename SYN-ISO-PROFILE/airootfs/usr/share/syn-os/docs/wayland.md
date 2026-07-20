# Wayland vs X11: What Changed and Why

For a general explanation of what Wayland and X11 actually are, see [What is Wayland?](./concepts/wayland.md) and [Window Manager](./concepts/window-manager.md). This page covers what changed specifically in SYN-OS's own desktop stack, and why — it is not a keybind or config reference; for that see [LabWC](./labwc.md) and [Waybar](./waybar.md).

## Current state

SYN-OS runs entirely on Wayland today. [LabWC](https://github.com/labwc/labwc) is the session compositor, [Waybar](./waybar.md) is the panel, and every helper script in `/usr/lib/syn-os/` targets Wayland protocols directly. There is no X11 code path anywhere in the current tree — no `Xwrangler`, no `xrandr`/`xdotool` dependency, no `.xinitrc`, no XWayland compatibility layer configured. Anything that used to run under X11 was either replaced outright or dropped.

## What moved, and to what

| Role | Before (X11) | Now (Wayland) |
|---|---|---|
| Compositor / window manager | Openbox, running on top of a separate X server process | [LabWC](./labwc.md) — both the compositor and the window manager in one process, no X server involved |
| Panel | Tint2 | [Waybar](./waybar.md), using wlroots-native protocols (`wlr/workspaces`, `wlr/window`) that only exist on wlroots-based compositors like LabWC |
| App launcher | a dmenu-family X11 launcher | `wmenu`/`wmenu-run` (Wayland-native dmenu equivalent), invoked by `syn-bar-launcher.zsh`; `fuzzel` is also installed as a second, independent launcher (`Super+a` in `rc.xml`) |
| Session bring-up | `startx` / `.xinitrc` launching Openbox | the `synos` alias in [`.zshrc`](./zsh.md): `dbus-run-session -- env XDG_SESSION_TYPE=wayland labwc` |
| Screen locking / screenshots / recording | X11 tools (e.g. `scrot`, `i3lock`-family) | `swaylock`, `grim`+`slurp` ([Screenshot and Recording](./tools/screenshot-and-recording.md)), `wf-recorder` — all Wayland-native, all requiring a wlroots compositor to function |
| Output/display management | `xrandr` | `wlr-randr`, driving both the Display & Screens pipe menu and the persisted-output-state logic in [LabWC's `autostart`](./labwc.md#autostart-session-startup-logic) |

## Why LabWC specifically

LabWC was chosen over other wlroots-based compositors (Sway, river, etc.) because it deliberately reuses Openbox's configuration and theme format: the same XML tag names for keybinds and mousebinds in `rc.xml`/`menu.xml`, and the same Openbox `themerc` theme-directory structure under `usr/share/themes/`. This is why `SYN-OS-RED`, originally an Openbox theme, still works essentially unmodified as LabWC's default theme — the theme-continuity story only holds because LabWC chose Openbox-compatibility as a design goal, not by coincidence. That compatibility only covers config *format*, though; LabWC is a separate project with its own protocol support and its own limitations, not a drop-in Openbox replacement (see the caveat below).

## Why the migration mattered

X11 has no first-class multi-monitor DPI/scaling story, no compositor-level input security model, and requires a separate long-running server process a Wayland compositor doesn't need — the compositor *is* the display server. For a distro that ships its own screenshot/recording/output-management tooling as first-party scripts (not third-party X11 utilities glued together), targeting Wayland's protocols directly means those scripts talk to one process (the compositor) instead of coordinating an X server plus a window manager plus a compositing manager as three separate pieces. It also removes an entire dependency surface — no X server package, no X11 utility libraries — from what `SYNSTALL` has to pull in.

## If you're modifying SYN-OS

Any documentation, forum post, or personal notes referencing Openbox's `rc.xml`/`menu.xml` semantics, Tint2 config, or `.xinitrc` describe a desktop stack this repo no longer ships — none of it applies here even where the file format looks identical. LabWC's config *looks* like Openbox's, close enough that copy-pasting snippets from Openbox docs will often parse without error, but it is a distinct project with its own protocol support and its own gaps. Check [LabWC's own documentation](https://github.com/labwc/labwc/wiki) rather than assuming 1:1 Openbox compatibility for anything not already present in this repo's `rc.xml`/`menu.xml` — see [LabWC](./labwc.md) for what's actually configured today.
