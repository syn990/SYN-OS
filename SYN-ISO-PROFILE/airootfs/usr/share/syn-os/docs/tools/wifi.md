# Wi-Fi Picker (`syn-wifi`)

SYN-OS has no NetworkManager applet or standalone wifi GUI. Wireless
scan/connect is a compiled C program, `syn-wifi`, wired to waybar's
`network` module as its `on-click` action, and also reachable from the
live installer shell as `synos-wifi` (see [Zsh](../zsh.md)) since it's
just an ncurses TUI, not a rofi popup — it needs no compositor. It talks
to `iwd` directly over D-Bus (`net.connman.iwd`), not through the `iwctl`
CLI — no subprocess, no ANSI-stripping, no column-parsing.

Source lives at `SYN-SOFTWARE/syn-wifi-src/` (a sibling of
`SYN-ISO-PROFILE/` and `BUILD-ARCHISO.zsh` at the repo root, not inside
`airootfs`), built once at ISO-build time like every other
locally-authored native tool — see [Building the ISO](../build/iso-builder.md).

## Invocation

`syn-wifi` takes no arguments. On the installed desktop it's launched by
clicking the `network` module in the bar, in a `foot` window (since it's
a real terminal UI, not a popup):

```jsonc
"network": {
  "format-wifi": "  {essid}  {bandwidthUpBytes}/{bandwidthDownBytes}",
  "format-ethernet": "󰈀 {ipaddr}  {bandwidthUpBytes}/{bandwidthDownBytes}",
  "format-disconnected": "󰤮",
  "tooltip-format": "{ifname} {ipaddr}/{cidr}",
  "interval": 2,
  "on-click": "foot -e /usr/lib/syn-os/syn-wifi"
}
```

No `doas`/root needed: everything `syn-wifi` does — device discovery,
scanning, listing, connecting (including registering the
`net.connman.iwd.Agent` used for password entry, see Step 4 below) — is
already reachable by the `wheel`/`network` groups per `iwd`'s own D-Bus
policy (`/usr/share/dbus-1/system.d/iwd-dbus.conf`). The `root`-only
carve-out in that file governs `iwd` itself sending with the `Agent`
interface, not a plain user's process receiving `iwd`'s callback on its
own exported Agent object — confirmed by running `syn-wifi` as a normal
user end-to-end, including a fresh `RegisterAgent` + `Connect()` +
`RequestPassphrase` round trip.

The module itself (icon/essid/bandwidth text) is waybar's own built-in
`network` module — `syn-wifi` only handles the click, not the bar display.

In the live installer shell, the same binary is aliased as `synos-wifi`
in `/etc/zsh/zshrc` for the same reason — no compositor exists there for
a rofi popup to render into, but an ncurses TUI runs in the bare TTY just
fine.

## Step 1: find the wireless device

`syn_iwd_open()` calls `org.freedesktop.DBus.ObjectManager.GetManagedObjects`
on `net.connman.iwd` at `/`, walks the returned object tree, and returns
the first object exposing `net.connman.iwd.Device` whose `Mode` property
is `"station"` — the direct D-Bus equivalent of the old script's `iwctl
device list | awk '$5=="station"'` search, but reading real typed
properties instead of parsing a colored text table. If no such device
exists, `syn-wifi` prints an error and exits.

## Step 2: scan

`syn_iwd_scan()` calls `Station.Scan()`, then polls the `Station.Scanning`
boolean property (via the bus file descriptor and `sd_bus_process`, not a
fixed `sleep`) until it flips back to `false` or a 15-second timeout
elapses. `Scan()` itself is asynchronous — it returns as soon as `iwd`
accepts the request, not once results are in — so this replaces the old
script's flat `sleep 3` guess with an actual "wait for iwd to say it's
done" signal.

## Step 3: get networks

`syn_iwd_get_networks()` calls `Station.GetOrderedNetworks()`, which
returns an array of `(object_path, signal_strength)` pairs already sorted
strongest-first by `iwd` itself — no client-side sorting needed. For each
network object, one `org.freedesktop.DBus.Properties.GetAll` call on
`net.connman.iwd.Network` pulls `Name` (the real SSID, already UTF-8
decoded by `iwd`), `Type` (`"psk"` / `"open"` / `"8021x"` / ...), and
`Connected`. `syn_iwd_signal_bars()` maps the raw signal strength (roughly
dBm × 100, e.g. `-6200` for -62dBm) to a 0-4 bar count for display, using
the same rough thresholds `iwctl`'s own asterisk column and
NetworkManager use.

The TUI (`syn_wifi_tui_network_list()`) renders this as a scrollable list
— SSID, security type, a 4-cell signal bar glyph, and a `●` marker on
whichever network is currently connected — themed from the live SYN-OS
palette exactly like [syn-crypter](./syn-crypter.md)'s dashboard (both
share `syn_theme.c`, read from `~/.config/syn-os/current-theme`).
`↑`/`↓`/`j`/`k` move the selection, `Enter` connects, `r` re-scans,
`Esc`/`q` quits.

## Step 4: connect

`syn_iwd_connect()` calls `Network.Connect()` on the chosen network. If
`iwd` needs a passphrase it doesn't already have stored, `Connect()`
doesn't take one as an argument — instead `iwd` calls back into a
`net.connman.iwd.Agent` object the caller must itself export over D-Bus.
`syn-wifi` registers exactly one such object (`/syn/wifi/agent`) for the
duration of a single `Connect()` call: iwd invokes its
`RequestPassphrase(o path) -> s passphrase` method (this exact signature
isn't documented anywhere iwd ships — it was confirmed empirically against
a live `iwd` instance during development), which shows
`syn_wifi_tui_password_prompt()` (a masked line-input screen, same shape
as [syn-crypter](./syn-crypter.md)'s password prompt) and replies with
whatever was typed. If the network is already known to `iwd` (as with a
network already connected once before), the agent is never called at all
— confirmed by testing a reconnect against an already-known network and
observing the password callback correctly never fire.

On success or failure, `syn_wifi_tui_message()` shows a result screen
before returning to the network list — a failed attempt (wrong password,
unreachable AP, etc.) doesn't exit the picker, so another attempt or a
different network is one keypress away.

## Summary of the flow

```
launch syn-wifi (waybar click, or synos-wifi in the live shell)
  -> find station-mode device (D-Bus ObjectManager, Device.Mode)
  -> Station.Scan(), poll Station.Scanning until false (no fixed sleep)
  -> Station.GetOrderedNetworks() + per-network Properties.GetAll
       => sorted, structured network list (name, type, signal, connected)
  -> ncurses network list (signal bars, security, connected marker)
  -> Enter on a network: Network.Connect()
       -> if a passphrase is needed: iwd calls back into syn-wifi's own
          registered Agent (RequestPassphrase), masked TUI prompt answers it
  -> result screen, back to the list
```

## Dependencies

`iwd` (the D-Bus service itself — `net.connman.iwd`), `systemd`
(`libsystemd`/`sd-bus`, the D-Bus client library `syn-wifi` links
against), `ncurses` (`ncursesw`). No `iwctl` subprocess, no `rofi`.
