# Waybar: The Panel

Waybar is the top panel started by [LabWC's autostart](./labwc.md#key-files). Config lives at `DotfileOverlay/etc/skel/.config/waybar/`: `config.jsonc` (modules and their behavior) and `style.css` (appearance). The scripts backing the `custom/*` modules live under `DotfileOverlay/usr/lib/syn-os/` instead (named `syn-bar-*.zsh`) — not `.config/waybar/` itself, since they're executable logic rather than DE configuration.

## Layout

```
[glyph] [workspaces]          [window title] [media]          [vpn] [network] [backlight] [volume] [cpu] [memory] [temp] [disk] [battery] [clock] [power]
```

- **Left:** `custom/glyph` (see below) and `wlr/workspaces` (the 4 desktops named in [LabWC's `rc.xml`](./labwc.md), rendered as icons `🃁 ℐ ℕ ⌘`).
- **Center:** the focused window's title (`wlr/window`) and `custom/media` (now-playing via `playerctl`; click to play/pause, scroll to skip).
- **Right:** `network` (shows up/down bandwidth via Waybar's built-in `{bandwidthUpBytes}`/`{bandwidthDownBytes}`, no external script) and `backlight`/`pulseaudio` (user-facing controls) grouped ahead of the passive stats cluster (`cpu`/`memory`/`temperature`/`custom/disk`), then `battery`, `clock`, and `custom/power` (click runs `syn-bar-power.zsh`, see below).

## Custom modules

These are the pieces that don't ship with Waybar itself: small `syn-bar-*.zsh` scripts under `/usr/lib/syn-os/`, referenced by `exec`/`on-click` in `config.jsonc`:

- **`custom/glyph`** (top-left): a fixed, per-theme icon, not a rotator — `exec` just `cat`s `~/.config/waybar/glyph`, a one-line file `syn-theme-apply` writes from the active theme's `SYN_GLYPH` (e.g. `●` for `SYN-OS-RED`, `❄` for `SYN-OS-BLUE`, empty for several others, falling back to `●`). It only changes when you switch themes, at which point `syn-theme-apply` rewrites the file and sends waybar `SIGUSR2` to reload — no interval, no timer. Clicking it opens `syn-bar-launcher.zsh` with the bar's colors passed as arguments; this is the application launcher, not just decoration.
- **`syn-bar-wifi.zsh`** (`network.on-click`): detects the active wireless device via `iwctl device list` (first device in `station` mode), scans, strips ANSI codes and header lines from `get-networks`, feeds the SSID list into `wmenu` for selection, then opens a `foot` terminal running `iwctl station <iface> connect <ssid>` so you can type a passphrase interactively.
- **`custom/vpn`**: checks for a `wg0` interface (WireGuard) as a proxy for "VPN connected," shown as a lock glyph when up.
- **`custom/disk`**: `df -h /` used-of-total, refreshed every 60s.
- **`syn-bar-power.zsh`** (`custom/power.on-click`): a `rofi -dmenu` picker (Lock / Log Out / Reboot / Power Off), not `wlogout` — `wlogout` is AUR-only and pacstrap can't reach the AUR, so it was never installable via [`SYNSTALL`](./packages.md). The script sources the active theme's `.theme` file fresh on every click (same read-on-click pattern as `syn-bar-launcher.zsh`/`syn-bar-wifi.zsh`, see [theming diagram](./labwc.md#how-a-theme-reaches-every-consumer)) and builds a `-theme-str` argument overriding rofi's root-level CSS variables (`background`, `foreground`, `lightbg`, `selected-normal-background`, `border-color`), not just individual widget selectors. Rofi's stock theme is built on those root variables, so overriding only `window`/`element selected` would leave everything else — entry box, unselected rows — on rofi's default light palette regardless of the active SYN-OS theme. Positioned top-right near the power icon via `-location 3` with a small offset, semi-transparent (`${SYN_BG}e6` alpha suffix).

## Styling

`style.css` is a dark/maroon theme matching LabWC's `SYN-OS-RED` theme (`#400101`/`#260101`/`#800000` accents on black), using FontAwesome/Terminus/Noto Color Emoji for icon and text rendering. Workspace buttons highlight on hover/focus; the battery module blinks when critical and not charging. Hover states across the bar use the theme's `SYN_PANEL_HOVER` variable, a lighter shade of the panel color, not `SYN_BG_ALT`. All four shipped themes (`SYN-OS-RED/BLUE/GREEN/PURPLE`) keep a pure-black `SYN_BG` and differ only in panel/accent/hover shades.

## Customizing

Add a module by editing `config.jsonc`'s `modules-left/center/right` arrays and defining its config block; style it in `style.css` using the module's CSS ID (`#custom-yourmodule`). As with LabWC, edits only reach a real system through [`DotfileOverlay`](./dotfile-overlay.md) on rebuild/install, not by hand-editing a live `~/.config/waybar/`.
