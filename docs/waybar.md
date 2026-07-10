# Waybar: The Panel

Waybar is the top panel started by [LabWC's autostart](./labwc.md#key-files). Config lives at `DotfileOverlay/etc/skel/.config/waybar/`: `config.jsonc` (modules and their behavior), `style.css` (appearance), and several custom shell scripts backing the `custom/*` modules.

## Layout

```
[glyph] [workspaces]          [window title] [media]          [vpn] [network] [net-speed] [cpu] [memory] [temp] [disk] [backlight] [volume] [battery] [clock] [power]
```

- **Left:** `custom/glyph` (see below) and `wlr/workspaces` (the 4 desktops named in [LabWC's `rc.xml`](./labwc.md), rendered as icons `🃁 ℐ ℕ ⌘`).
- **Center:** the focused window's title (`wlr/window`) and `custom/media` (now-playing via `playerctl`; click to play/pause, scroll to skip).
- **Right:** a chain of system status modules, ending in `custom/power` (click opens `wlogout`, which isn't in [`SYNSTALL`](./packages.md); same category of gap as `fuzzel`/`swaylock` noted in the LabWC doc).

## Custom modules

These are the pieces that don't ship with Waybar itself: small scripts in the same directory, referenced by `exec` in `config.jsonc`:

- **`glyph-rotator.sh`** (`custom/glyph`, top-left): cycles through a fixed array of Unicode symbols (☯ ⚛ ⚙ ♞ ★ etc.), picking one deterministically based on the current time (`(epoch / 20) % count`), so it changes every 20 seconds but is identical across bar instances at the same moment. Clicking it opens `wmenu-run` with the bar's colors passed as arguments; this is the application launcher, not just decoration.
- **`net-speed.sh`** (`custom/net`): detects the active interface with `ip -o link show up | awk -F': ' '$2 != "lo" {print $2}' | head -n1` (first non-loopback interface that's up), samples `/sys/class/net/<iface>/statistics/{rx,tx}_bytes` a second apart, and prints the KB/s delta. Works on wired or wireless; prints `-- --` if nothing is up.
- **`wifi-menu.sh`** (`network.on-click`): scans with `iwctl station wlan0 scan`, strips ANSI codes and header lines from `get-networks`, feeds the SSID list into `wmenu` for selection, then opens a `foot` terminal running `iwctl station wlan0 connect <ssid>` so you can type a passphrase interactively. **Hardcoded to interface `wlan0`**: on a Wi-Fi card that enumerates differently, this module won't find a network to scan.
- **`custom/vpn`**: checks for a `wg0` interface (WireGuard) as a proxy for "VPN connected," shown as a lock glyph when up.
- **`custom/disk`**: `df -h /` used-of-total, refreshed every 60s.

## Styling

`style.css` is a dark/maroon theme matching LabWC's `SYN-OS-RED` theme (`#400101`/`#260101`/`#800000` accents on black), using FontAwesome/Terminus/Noto Color Emoji for icon and text rendering. Workspace buttons highlight on hover/focus; the battery module blinks when critical and not charging.

## Known interface assumption

`wifi-menu.sh` assumes the wireless interface is named `wlan0`. This works on the maintainer's hardware but is not detected dynamically. If you're adapting SYN-OS for a machine where the interface enumerates differently (common with some USB Wi-Fi adapters or predictable-naming schemes), edit the `INTERFACE=` line at the top of the script. `net-speed.sh` doesn't have this problem: it detects the active interface at runtime.

## Customizing

Add a module by editing `config.jsonc`'s `modules-left/center/right` arrays and defining its config block; style it in `style.css` using the module's CSS ID (`#custom-yourmodule`). As with LabWC, edits only reach a real system through [`DotfileOverlay`](./dotfile-overlay.md) on rebuild/install, not by hand-editing a live `~/.config/waybar/`.
