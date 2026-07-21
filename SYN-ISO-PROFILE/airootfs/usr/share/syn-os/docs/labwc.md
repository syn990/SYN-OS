# LabWC: Window Manager

[LabWC](https://github.com/labwc/labwc) is the Wayland compositor and window manager SYN-OS runs — see [Wayland vs X11](./wayland.md) for why it replaced Openbox+X11. LabWC deliberately reuses Openbox's XML config format and theme structure, so `rc.xml`/`menu.xml` will look familiar if you've configured Openbox before, even though it's a distinct project underneath.

Config lives at `DotfileOverlay/etc/skel/.config/labwc/` in the repo, deployed to `~/.config/labwc/` on the installed system (see [Dotfile Overlay](./dotfile-overlay.md) for the deployment mechanics). Five files live there: `rc.xml`, `menu.xml`, `autostart`, `environment`, `themerc`.

## `rc.xml`: theme, desktops, mouse, keybinds

**Theme**: `<theme><name>SYN-OS-RED</name>` — `themerc` (below) is the Openbox-format theme definition this name resolves to. Titlebar button layout is `NLIMC` (window-menu, then Left-aligned buttons end with Iconify/Maximize/Close on the right — `titleLayout` follows Openbox's own letter codes). Font is Terminus 10pt everywhere (Bold for `ActiveWindow` and `MenuHeader`, regular for `InactiveWindow`, `MenuItem`, `OnScreenDisplay`).

**Window rules**: one rule, targeting windows with `identifier="syn-os-popup"` — these are `foot` windows launched via `syn-popup-lib.zsh` by every menu tool that runs a command and shows the result (SYN-SHARE, the BlackArch/services toggles, the ISO builder, crypter/redshirt/graphmap). The rule strips server-side decoration and forces a centered `640x360` window (`ResizeTo` + `AutoPlace policy="center"`), so these read as small popups rather than resizable terminal windows.

**Virtual desktops**: `<desktops number="4">`, named with glyphs:

| # | Name |
|---|---|
| 1 | `🃁 [Terminal]` |
| 2 | `ℐ [Web]` |
| 3 | `ℕ [Media]` |
| 4 | `⌘ [External]` |

These exact glyphs are echoed in Waybar's `wlr/workspaces` module (`format-icons` in `config.jsonc`) — see [Waybar](./waybar.md#layout).

**Mouse bindings**:

| Binding | Action |
|---|---|
| `Alt+Left` drag | Move window |
| `Alt+Right` drag | Resize window |
| `Alt+Middle` | Lower window |

**Keybinds** — every `<keybind key=...>` entry in `rc.xml`, in file order:

| Key | Action |
|---|---|
| `Super+Space` | Show root menu (`menu.xml`) |
| `Super+Escape` | Reconfigure (reload config without restarting session) |
| `Super+l` | `swaylock -f -c 1a0000` (lock screen) |
| `Super+Shift+e` | Exit LabWC |
| `Super+Return` | Launch `foot` (terminal) |
| `Super+a` | Launch `fuzzel` (app launcher) |
| `Super+e` | Launch `syn-filemanager` |
| `Super+q` | Close focused window |
| `Alt+Tab` | Next window |
| `Alt+Shift+Tab` | Previous window |
| `Super+Tab` | Toggle maximize |
| `Super+Shift+Left/Right/Up/Down` | Move window to screen edge |
| `Ctrl+Alt+Left/Right` | Go to desktop left/right (no wrap) |
| `Shift+Alt+Left/Right` | Send window to desktop left/right (no wrap) |
| `Super+1`..`Super+4` | Go to desktop 1–4 |
| `Super+p` | `/usr/lib/syn-os/screenshot.zsh region` |
| `Super+Shift+p` | `/usr/lib/syn-os/screen-recorder.zsh full` |

Screenshot and recording behavior (output paths, the recorder's start/stop PID-file toggle) are documented in full in [Screenshot and Recording](./tools/screenshot-and-recording.md) — the keybinds above are the only overlap with this page.

Two launchers worth noting since they're easy to conflate: `Super+Return` opens `foot` directly, while `Super+a` opens `fuzzel`, a separate app launcher from the `wmenu`/`wmenu-run` one Waybar's own launcher click runs (`syn-bar-launcher.zsh`). Both are installed; they're independent entry points, not aliases of each other.

## `menu.xml`: the root menu (`Super+Space`)

![The root menu open over the desktop](./screenshots/menu-xml-root-open.png)
*Placeholder — `Super+Space` pressed, showing the top-level Applications /
Capture / SYN-OS Tools / Preferences / System / Power structure.*

Structure, top to bottom:

**Applications** — `Terminal` (`foot`), `File browser` (`syn-filemanager`), and `All Applications`, a dynamic pipe submenu (`xdg_menu --format openbox3-pipe --root-menu /etc/xdg/menus/arch-applications.menu`) covering everything installed that isn't a SYN-OS tool. A `BLACKARCH-MENU-START`/`BLACKARCH-MENU-END` comment pair marks where `syn-blackarch-toggle.zsh`'s Enable action inserts a BlackArch application block — absent by default, so nothing advertises BlackArch tools on a system that doesn't have the repo enabled. See [BlackArch Toggle](./tools/blackarch-toggle.md).

**Capture** — a dedicated submenu for screen capture, separate from Applications:
- `Screenshot - full screen` / `Screenshot - select area`
- `Record - full screen` / `Record - select area / Stop` (the same entry starts and stops a region recording)

Full behavior in [Screenshot and Recording](./tools/screenshot-and-recording.md).

**SYN-OS Tools** submenu — the custom scripts under `/usr/lib/syn-os/` with no other GUI entry point. Every entry that runs an interactive TUI wraps it so a failure leaves the terminal open instead of the window just vanishing:
- `SYN-SHARE (file transfer hub)` — dynamic pipe submenu (`syn-pipe-share.zsh`)
- `SYN-CRYPTER (AES/Blowfish/RSA file encryption)` — `syn-crypter-prompt.zsh`
- `SYN-REDSHIRT (Uplink-style XOR obfuscation)` — `syn-redshirt-prompt.zsh`
- `SYN-GRAPHMAP (graph a directory tree)` submenu — `Quick (shallow scan)`, `Full (deep scan)`, `Custom (enter a depth)`
- `SYN-OS ISO Builder` — full decorated terminal (`foot -e zsh -c 'zsh /usr/lib/syn-os/syn-build-launcher.zsh; exec zsh'`), since a build runs for minutes with real scrolling output, unlike every other tool's one-line popup result

**Preferences** submenu, split into three labeled groups:
- *Desktop*: `Audio Settings`, `Display & Screens`, `Themes` — each a live dynamic pipe menu (`syn-pipe-audio.zsh`, `syn-pipe-display.zsh`, `syn-pipe-theme.zsh`), plus a plain `Toggle Bar Position` item (`syn-bar-toggle-position.zsh`)
- *Software*: `BlackArch` (`syn-blackarch-toggle.zsh`) and `Services (enable/disable any systemd unit)` (`syn-services-toggle.zsh`) — both live toggles on an already-installed system, no reinstall needed
- *Reference*: `Docs` — dynamic pipe menu (`syn-pipe-docs.zsh`) listing every doc under `/usr/share/syn-os/docs`, grouped into labeled sections per topic subdirectory (Theming, Tools, ISO Build, Concepts), rendered on selection by `syn-docs-view.zsh` (`glow` in a `foot` window, any referenced SVG diagram opened separately via `feh`)

![The Docs pipe-menu open, showing the topic-grouped sections](./screenshots/menu-docs-pipe-open.png)
*Placeholder — Preferences > Docs open, showing the flat top-level entries
followed by the Theming/Tools/ISO Build/Concepts separators this pipe menu
now generates.*

Theme application itself — how picking an entry in the Themes pipe menu reaches every app's config — is the dedicated subject of [Theme Engine](./theming/theme-engine.md); this page only lists where the menu entry lives.

**System** submenu:
- `Lock Screen` (`swaylock -f -c 1a0000`), `Volume Mixer` (`syn-audio`, a compiled ncurses audio TUI, in a `foot` window — see [syn-audio](./tools/audio.md))
- `Kill Process` submenu: `Close Focused Window`, `Kill All Terminals (foot)` (`pkill foot`), `Kill Web Browser (Falkon)` (`pkill falkon`), `Kill File Browser (syn-filemanager)` (`pkill syn-filemanager`) — a short, deliberately fixed list, not a process browser. (Waybar/LabWC have their own kill entries in the submenu below, not here.)
- `Edit Configuration Files` submenu, split into `Waybar Configurations` (`Waybar Config` → `featherpad ~/.config/waybar/config.jsonc`; `Waybar Style (overwritten by Themes switch)` → `featherpad ~/.config/waybar/style.css`, labeled as such because `syn-theme-apply` overwrites `style.css` wholesale on every theme switch) and `LabWC Configurations` (`Labwc Menu`, `Labwc RC`, `Labwc Autostart`, each opening the corresponding file in `featherpad`)
- `Waybar & LabWC` submenu: `Reload LabWC Configuration` (`Reconfigure` action, same as `Super+Escape`), `Return to Tty1 (Kill LabWC)` (`pkill labwc`), `Launch Waybar`, `Kill Waybar`

**Power**: `Reboot` (`systemctl reboot`), `Power Off` (`systemctl poweroff`).

## `autostart`: session startup logic

LabWC allows exactly one autostart file, run right after it reads its config. It has no shebang and executes under `/bin/sh`, not zsh — this is why `syn-theme-lib.zsh` (sourced at the top) is written in the POSIX-sh subset both shells understand, not zsh-specific syntax. The file defines two real functions and then calls them in sequence:

**`apply_persisted_display_state()`** — restores outputs the user previously turned off via the Display & Screens menu. Reads `~/.config/syn-os/disabled-outputs` (one `wlr-randr` output name per line) if it exists and `wlr-randr` is available. It parses `wlr-randr`'s live output with `awk`, anchoring each output's enabled/disabled state on that output's own block-start line (a line beginning with the output name) rather than proximity-based grep, since the gap to the `Enabled:` line varies per output. For each persisted output name: if it's currently enabled and more than one output total is enabled, it gets turned off (`wlr-randr --output <name> --off`); if the total enabled count is zero, it gets turned back on instead — a persisted "off" state can never leave the session with zero active displays.

**`bootstrap_or_relaunch_theme()`** — branches on whether `~/.config/syn-os/current-theme` already exists, and either way starts waybar exactly once as part of that branch:
- If it exists (not first login): sources the theme via `syn_theme_load` (from `syn-theme-lib.zsh`), relaunches only the wallpaper (`swaybg -i "$SYN_WALLPAPER"`, backgrounded) — re-running the full `syn-theme-apply` on every login would be redundant since the theme is already applied everywhere — then starts `waybar &` directly.
- If it doesn't exist (first login): runs `syn-theme-apply SYN-OS-RED` to bootstrap the default theme into every consumer's config for the first time; `syn-theme-apply` itself starts waybar as part of that (see [Theme Engine](./theming/theme-engine.md)), so this branch doesn't start it again.

Waybar used to be started unconditionally as the file's own last line, in the foreground — this ran a *second* waybar process alongside the one `syn-theme-apply` already started on first login, since both paths tried to own starting it. Moving the launch into `bootstrap_or_relaunch_theme()` itself makes each branch responsible for starting exactly one waybar, and nothing in `autostart` needs to stay in the foreground to keep the session alive — labwc doesn't wait on autostart's own exit, only on the daemons it launches actually running. After `bootstrap_or_relaunch_theme()`, the file starts `mako &` (backgrounded — every `notify-send` call in SYN-OS depends on mako being alive, so it has to start before anything else that might fire a toast) and then exits. See [Notifications](./tools/notifications.md) for mako's own config and theming.

## `environment`: process environment for the session

LabWC parses `environment` itself — it is **not** sourced as a shell script the way Openbox's equivalent is. Plain `KEY=value` lines only; no `export`, no quotes, either literally ends up in the value.

The current file sets one variable:

```
QT_QPA_PLATFORMTHEME=qt6ct
```

This is **qt6ct**, not qt5ct. Every Qt application SYN-OS ships (Falkon, syn-filemanager) links against Qt6, which cannot load the Qt5-only qt5ct platform-theme plugin at all — a comment in the file itself notes this explicitly. Both `~/.config/qt5ct/` and `~/.config/qt6ct/` exist in the dotfile overlay (qt5ct's config is kept for any Qt5 binary that might still get installed by hand), but `environment` only ever points the session at qt6ct.

## `themerc`: the Openbox-format theme file

`rc.xml`'s `<theme><name>SYN-OS-RED</name>` resolves to `usr/share/themes/SYN-OS-RED/openbox-3/themerc` in the overlay — colors, borders, and button styles in Openbox's theme format, which LabWC reads natively. Only `SYN-OS-RED` ships a `themerc`; the other 13 shipped themes apply entirely through `SYN_*` shell variables and rendered templates instead, with no separate Openbox theme directory of their own.

How a `.theme` file's variables reach `themerc`-adjacent consumers (Waybar CSS, qt6ct colors, foot, and — for `SYN-OS-RED` specifically — this `themerc`) at click-time, with no daemon and no polling, is documented in full in **[Theme Engine](./theming/theme-engine.md)**. This page intentionally stops at "here's where the theme name is set and where the theme file lives" — the trickle-down mechanism itself lives there.

## Customizing

Edit `rc.xml`/`menu.xml`/`autostart`/`environment` directly, then either `Super+Escape` (`Reconfigure`) to reload without restarting the session, or log out and back in for anything `Reconfigure` doesn't cover (like `environment`, which only takes effect on session start). These are dotfile *templates* in the repo sense: changes under `DotfileOverlay/etc/skel/.config/labwc/` only reach a real system at install/rebuild time — see [Dotfile Overlay](./dotfile-overlay.md) — editing a running system's `~/.config/labwc/` directly only affects that one machine.
