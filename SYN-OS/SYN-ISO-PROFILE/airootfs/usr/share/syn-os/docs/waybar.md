# Waybar: The Panel

Waybar is the top panel started as the last line of [LabWC's `autostart`](./labwc.md#autostart-session-startup-logic). Config lives at `DotfileOverlay/etc/skel/.config/waybar/`: `config.jsonc` (modules and behavior) and `style.css` (appearance), deployed to `~/.config/waybar/` (see [Dotfile Overlay](./dotfile-overlay.md)). The scripts backing every `custom/*` module live under `DotfileOverlay/usr/lib/syn-os/` instead, named `syn-bar-*.zsh` — executable logic, not DE configuration, so they don't live inside `.config/waybar/` itself.

## Layout

`config.jsonc` sets `"height": 28` and `"spacing": 6`, with modules split three ways:

```
[glyph] [workspaces]        [window title]        [recording] [synshare] [vpn] [network] [backlight] [volume] [cpu] [memory] [temp] [disk] [battery] [clock] [power]
   \_______________/              |                \_______________________________________________________________________________________________________/
      modules-left            modules-center                                                       modules-right
```

- **`modules-left`**: `custom/glyph`, `wlr/workspaces` (the 4 desktops named in [LabWC's `rc.xml`](./labwc.md#rcxml-theme-desktops-mouse-keybinds), rendered with `format-icons` `1`→`🃁`, `2`→`ℐ`, `3`→`ℕ`, `4`→`⌘`, default `•`; `all-outputs: true`, scroll enabled).
- **`modules-center`**: `wlr/window` only — the focused window's title, capped at 60 characters, `separate-outputs: true`.
- **`modules-right`**: `custom/recording`, `custom/synshare`, `custom/vpn`, `network`, `backlight`, `pulseaudio`, `cpu`, `memory`, `temperature`, `custom/disk`, `battery`, `clock`, `custom/power`, in that order.

![Waybar close-up, full bar width](./screenshots/waybar-closeup.png)
*Placeholder — the full bar at default height (28px), showing the
left/center/right module groups described below.*

## Custom modules

The pieces that don't ship with Waybar itself — small scripts under `/usr/lib/syn-os/`, wired in via `exec`/`on-click` in `config.jsonc`:

- **`custom/glyph`** (top-left): `exec` runs `cat $HOME/.config/waybar/glyph 2>/dev/null || echo ●` — a fixed, per-theme icon file `syn-theme-apply` writes from the active theme's `SYN_GLYPH` variable, not a rotator. It listens on `"signal": 2`, so `syn-theme-apply` can push an immediate refresh (`pkill -SIGRTMIN+2 waybar`-style) on theme switch instead of waiting for a poll interval. Clicking it runs `syn-bar-launcher.zsh` — the application launcher (themed `wmenu-run`, docked to whichever edge the bar is currently on), not just decoration.

- **`custom/recording`**: shows `Recording` when `screen-recorder.zsh` has an active recording, empty otherwise. `exec` checks for `$XDG_RUNTIME_DIR/syn-screen-recorder.pid` and whether that PID is still alive (`kill -0`), polled every 2 seconds. Clicking it runs `screen-recorder.zsh full`, which — because the script's own PID-file check runs first — stops the active recording rather than starting a second one. Full recording behavior (start/stop toggle logic, output paths, region vs. full-screen, optional audio device) is documented in [Screenshot and Recording](./tools/screenshot-and-recording.md); this module is just its live indicator.

- **`custom/synshare`**: JSON-mode module (`"return-type": "json"`) polled every 5 seconds, backed by `syn-bar-share-status.zsh` — counts how many of the six SYN-SHARE-related systemd units (`rsyncd`, `smb`, `nfs-server`, `synshare-httpd`, `synshare-tftpd`, `synshare-nc`) are active and emits `{"text": "⇄ N", "tooltip": ..., "class": "active"|""}` (empty text when the count is zero). Clicking it runs `syn-bar-share-quickmenu.zsh`, a themed rofi popup offering Start/Stop per service (never both for the same service at once), a "Set / Probe Server" entry, "rsync: Pull", "Stop ALL services", and "Service Status" (opens a `foot` window running `syn-share.zsh status`). Full SYN-SHARE behavior is documented in [SYN-SHARE](./tools/syn-share.md); this module and its quick-menu are the bar-level summary and shortcut, not the full picture.

- **`custom/vpn`**: checks for a live `wg0` interface as a proxy for "WireGuard VPN connected," polled every 5 seconds, backed by the compiled `syn-bar-vpn` (source under `/usr/lib/syn-os/syn-bar-vpn-src/`, built natively in Stage 1 like `syn-bar-window-title` below) — a one-line `if_nametoindex("wg0")` check, replacing the old inline `ip link show wg0` shell exec. Empty format string when down, so it disappears from the bar entirely rather than showing an explicit "off" state.

- **`custom/disk`**: JSON-mode, polled every 60 seconds, backed by the compiled `syn-bar-disk` (source under `/usr/lib/syn-os/syn-bar-disk-src/`, built natively in Stage 1) — root filesystem used/total via `statvfs(2)`, with `class` set to `warning` (≥75%) or `critical` (≥90%). Tooltip lists every real mount (tmpfs/devtmpfs/squashfs/overlay excluded, plus anything `statvfs` reports as zero-block) via `getmntent(3)`, replacing the old `syn-bar-disk.zsh`'s `df`/`awk`/`python3 -c` pipeline. Clicking it opens `foot -e spf /` (Superfile at the root).

- **`custom/power`**: static `⏻` glyph, no polling. Clicking it runs `syn-bar-power.zsh` — a themed `rofi -dmenu` picker (Lock / Log Out / Reboot / Power Off), not `wlogout`: `wlogout` is AUR-only, and pacstrap can't reach the AUR, so it was never installable via [`SYNSTALL`](./packages.md). The script sources the active theme's `.theme` file fresh on every click and builds a full root-level `-theme-str` override (`background`, `foreground`, `lightbg`, `selected-normal-background`, `border-color`) rather than overriding individual widget selectors — rofi's stock theme is built on those root variables, so a partial override would leave the entry box and unselected rows on rofi's default light palette regardless of the active SYN-OS theme. Positioned top-right (`-location 3`) near the power icon, semi-transparent via a `${SYN_BG}e6` alpha suffix on the background color.

  ![The rofi power menu open](./screenshots/rofi-power-menu.png)
  *Placeholder — Lock / Log Out / Reboot / Power Off, themed and positioned
  near the power icon as described above.*

Stock (non-custom) modules worth noting: `pulseaudio` (click opens `pavucontrol`, middle-click mutes via `pamixer -t`, scroll adjusts volume ±5%), `network` (click runs `syn-bar-wifi.zsh` — see [Wi-Fi](./tools/wifi.md) for its full `iwctl` scan/connect flow), `cpu`/`memory`/`temperature` (click opens `btop` in a `foot` window), `backlight` (scroll-only, `intel_backlight` device), and `battery` (blinks on critical + not charging).

## Styling

`style.css` is a dark/maroon theme matching LabWC's `SYN-OS-RED` theme: pure black (`#000000`) panel background with a `3px solid #400101` bottom border, `#2c0101` background on the stats cluster (`cpu`/`memory`/`temperature`/`custom-disk`/`network`/`pulseaudio`/`backlight`) separated by `1px solid #400101` borders, and `#400101`/`#800000` hover and focus accents on workspace buttons and hoverable modules. Font stack is `FontAwesome, Roboto, Terminus, "Noto Sans Symbols", "Noto Color Emoji"` at 14px (16px for `custom-glyph`). The battery module blinks (`animation: blink 0.5s steps(12) infinite alternate`) when critical and not charging.

This hand-written `style.css` is what ships in the repo, but it's also exactly what `syn-theme-apply` overwrites wholesale on every theme switch — it renders `theme-templates/waybar-style.css.tmpl` (or a theme-specific override template, e.g. for `SYN-OS-MATRIX`/`SYN-OS-WIN95`) using the active theme's `SYN_*` variables. The colors above are `SYN-OS-RED`'s values specifically, not hardcoded constants — the full variable contract and the render pipeline are covered in **[Theme Engine](./theming/theme-engine.md)**; this page only describes the visual approach, not the mechanism.

## Customizing

Add a module by editing `config.jsonc`'s `modules-left`/`modules-center`/`modules-right` arrays and defining its config block, then style it in `style.css` using the module's CSS ID (`#custom-yourmodule`). As with LabWC, edits only reach a real system through [`DotfileOverlay`](./dotfile-overlay.md) on rebuild/install — editing a live `~/.config/waybar/` by hand only affects that one machine, and (per the note above) `style.css` specifically gets overwritten the next time anyone switches themes on that machine regardless.
