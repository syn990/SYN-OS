# Waybar: The Panel

Waybar is the top panel started by [LabWC's autostart](./labwc.md#key-files). Config lives at `DotfileOverlay/etc/skel/.config/waybar/`: `config.jsonc` (modules and their behavior), `style.css` (appearance), and several custom shell scripts backing the `custom/*` modules.

## Layout

```
[glyph] [workspaces]          [window title] [media]          [vpn] [network] [backlight] [volume] [cpu] [memory] [temp] [disk] [battery] [clock] [power]
```

- **Left:** `custom/glyph` (see below) and `wlr/workspaces` (the 4 desktops named in [LabWC's `rc.xml`](./labwc.md), rendered as icons `🃁 ℐ ℕ ⌘`).
- **Center:** the focused window's title (`wlr/window`) and `custom/media` (now-playing via `playerctl`; click to play/pause, scroll to skip).
- **Right:** `network` (shows up/down bandwidth via Waybar's built-in `{bandwidthUpBytes}`/`{bandwidthDownBytes}`, no external script) and `backlight`/`pulseaudio` (user-facing controls) grouped ahead of the passive stats cluster (`cpu`/`memory`/`temperature`/`custom/disk`), then `battery`, `clock`, and `custom/power` (click runs `power-menu.sh`, see below).

## Custom modules

These are the pieces that don't ship with Waybar itself: small scripts in the same directory, referenced by `exec` in `config.jsonc`:

- **`glyph-rotator.sh`** (`custom/glyph`, top-left): cycles through a fixed array of Unicode symbols (☯ ⚛ ⚙ ♞ ★ etc.), picking one deterministically based on the current time (`(epoch / 20) % count`), so it changes every 20 seconds but is identical across bar instances at the same moment. Clicking it opens `wmenu-run` with the bar's colors passed as arguments; this is the application launcher, not just decoration.
- **`wifi-menu.sh`** (`network.on-click`): detects the active wireless device via `iwctl device list` (first device in `station` mode), scans, strips ANSI codes and header lines from `get-networks`, feeds the SSID list into `wmenu` for selection, then opens a `foot` terminal running `iwctl station <iface> connect <ssid>` so you can type a passphrase interactively.
- **`custom/vpn`**: checks for a `wg0` interface (WireGuard) as a proxy for "VPN connected," shown as a lock glyph when up.
- **`custom/disk`**: `df -h /` used-of-total, refreshed every 60s.
- **`power-menu.sh`** (`custom/power.on-click`): a `rofi -dmenu` picker (Lock / Log Out / Reboot / Power Off), not `wlogout` — `wlogout` is AUR-only and pacstrap can't reach the AUR, so it was never installable via [`SYNSTALL`](./packages.md). The script sources the active theme's `.theme` file fresh on every click (same read-on-click pattern as `wmenu-run.sh`/`wifi-menu.sh`, see [theming diagram](./labwc.md#how-a-theme-reaches-every-consumer)) and builds a `-theme-str` argument overriding rofi's root-level CSS variables (`background`, `foreground`, `lightbg`, `selected-normal-background`, `border-color`), not just individual widget selectors. Rofi's stock theme is built on those root variables, so overriding only `window`/`element selected` would leave everything else — entry box, unselected rows — on rofi's default light palette regardless of the active SYN-OS theme. Positioned top-right near the power icon via `-location 3` with a small offset, semi-transparent (`${SYN_BG}e6` alpha suffix).

## Styling

`style.css` is a dark/maroon theme matching LabWC's `SYN-OS-RED` theme (`#400101`/`#260101`/`#800000` accents on black), using FontAwesome/Terminus/Noto Color Emoji for icon and text rendering. Workspace buttons highlight on hover/focus; the battery module blinks when critical and not charging. Hover states across the bar use the theme's `SYN_PANEL_HOVER` variable, a lighter shade of the panel color, not `SYN_BG_ALT`. All four shipped themes (`SYN-OS-RED/BLUE/GREEN/PURPLE`) keep a pure-black `SYN_BG` and differ only in panel/accent/hover shades.

## Customizing

Add a module by editing `config.jsonc`'s `modules-left/center/right` arrays and defining its config block; style it in `style.css` using the module's CSS ID (`#custom-yourmodule`). As with LabWC, edits only reach a real system through [`DotfileOverlay`](./dotfile-overlay.md) on rebuild/install, not by hand-editing a live `~/.config/waybar/`.
