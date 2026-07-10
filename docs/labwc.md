# LabWC: Window Manager

[LabWC](https://github.com/labwc/labwc) is the compositor/window-manager SYN-OS runs on Wayland, the Openbox-alternative mentioned in the README. It's Openbox-style deliberately: LabWC reuses Openbox's theme format and a similar XML config structure, so if you've configured Openbox before, the files below will look familiar.

Config lives at `DotfileOverlay/etc/skel/.config/labwc/`, deployed to `~/.config/labwc/` on the installed system.

## Key files

**`rc.xml`**: the main config: theme selection, virtual desktops, mouse bindings, and keybinds. As shipped:

- Theme: `SYN-OS-RED` (see [Dotfile Overlay](./dotfile-overlay.md) for where theme files live), Terminus font throughout.
- 4 virtual desktops, named with glyphs: `🃁 [Terminal]`, `ℐ [Web]`, `ℕ [Media]`, `⌘ [External]`. These names are echoed in Waybar's `wlr/workspaces` module icons (see [Waybar](./waybar.md)).
- Mouse: `Alt+Left` drag to move, `Alt+Right` drag to resize, `Alt+Middle` to lower.
- Keybinds include: `Super+Space` opens the root menu, `Super+Return` opens a terminal (`foot`), `Super+q` closes the focused window, `Super+Tab` toggles maximize, `Alt+Tab`/`Alt+Shift+Tab` cycles windows, `Super+1..4` jumps to a desktop, `Ctrl+Alt+Left/Right` and `Shift+Alt+Left/Right` move between/send-to desktops, `Super+p` screenshots a selected region with `grim`+`slurp` into `~/Pictures/Screenshots/`.

  **Note:** `rc.xml` binds `Super+a` to launch `fuzzel`. `fuzzel` is not in [`SYNSTALL`](./packages.md); the package list installs `wmenu` as the launcher instead, which is what Waybar's glyph module and the wifi-menu script actually invoke. `Super+a` will do nothing on a stock install unless you separately install `fuzzel` or rebind that key to `wmenu-run`.

- `Super+l` locks the screen with `swaylock` (also not in `SYNSTALL`, same caveat applies).

**`autostart`**: runs when the `synos` alias launches the session (see [Zsh Configuration](./zsh.md)): sets the wallpaper via `swaybg` and starts `waybar`. Comment in the file notes this is the LabWC equivalent of Openbox's `~/.config/openbox/autostart`.

**`environment`**: sets process environment for the LabWC session; currently just `QT_QPA_PLATFORMTHEME=qt5ct` so Qt apps pick up the qt5ct-configured theme instead of a mismatched default.

**`menu.xml`**: the root menu shown by `Super+Space`.

**`themerc`**: the `SYN-OS-RED` Openbox-format theme definition (colors, borders, button styles) that `rc.xml` references by name; the actual theme files live under `usr/share/themes/SYN-OS-RED/openbox-3/` in the overlay (see [Dotfile Overlay](./dotfile-overlay.md)).

**`labwc-audio-menu.zsh`, `labwc-wlr-menu.zsh`, `spf-menu.zsh`**: helper scripts invoked from menu entries for audio device selection, output/monitor management, and launching the Superfile file manager.

## Customizing

Edit `rc.xml` for keybinds/theme/desktops, then either `Super+Escape` (bound to `Reconfigure`) to reload without restarting the session, or log out/in. Remember these are dotfile *templates*: changes here only reach a real system through [`DotfileOverlay`](./dotfile-overlay.md) at install/rebuild time, not by editing a running system's `~/.config/labwc/` directly (that copy is independent once deployed).
