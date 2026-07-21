# Audio Control (`syn-audio`)

SYN-OS has no `pavucontrol`. Volume/mute/default-device control is a
compiled C program, `syn-audio`, wired to waybar's `pulseaudio` module as
its `on-click` action and to the audio pipe menu's "Advanced Settings"
item. It talks to `pipewire-pulse` directly via `libpulse` — no `pactl`
subprocess, no text-parsing, no Qt runtime.

Source lives at `SYN-SOFTWARE/syn-audio-src/` (a sibling of
`SYN-ISO-PROFILE/` and `BUILD-ARCHISO.zsh` at the repo root, not inside
`airootfs`), built once at ISO-build time like every other
locally-authored native tool — see [Building the ISO](../build/iso-builder.md).

## Invocation

Run with no arguments for the interactive ncurses dashboard; flags drive
scripting/menu use (the pipe menu itself only ever calls the no-args
form, launched in a `foot` window):

```
syn-audio                                              (interactive)
syn-audio --list-sinks | --list-sources
syn-audio --set-default-sink|--set-default-source <name>
syn-audio --mute-sink|--mute-source <name> on|off|toggle
syn-audio --set-sink-volume|--set-source-volume <name> <0-100>
syn-audio --adjust-sink-volume|--adjust-source-volume <name> <+-N>
```

Waybar's `pulseaudio` module click:

```jsonc
"pulseaudio": {
  "format": "{volume}% {icon}",
  "on-click": "foot -e /usr/lib/syn-os/syn-audio",
  "on-click-middle": "pamixer -t",
  "on-scroll-up": "pamixer -i 5",
  "on-scroll-down": "pamixer -d 5"
}
```

Middle-click mute and scroll-to-adjust still go through `pamixer` — only
the full launcher (left-click) was replaced. The bar's own volume
percentage/icon is waybar's built-in `pulseaudio` module display;
`syn-audio` only handles the click.

## Connecting to the server

`syn_pulse_open()` (`syn_pulse.c`) creates a plain `pa_mainloop` (not
`pa_threaded_mainloop`) and connects with `pa_context_connect()`, then
iterates the mainloop synchronously until the context reaches
`PA_CONTEXT_READY`. Every `syn-audio` invocation is a single request or a
keypress-driven loop, never concurrent work, so there's no background
mainloop thread — an earlier threaded version measured roughly 25ms per
connect from thread-spawn and lock/condvar overhead alone, against ~3ms
for a plain mainloop doing the same handshake. The plain version now
matches `pactl`'s own per-call latency.

## Listing and control

Every operation follows the same shape: kick off a `pa_context_get_*`
call, iterate the mainloop until its callback marks a `done` flag, then
read the result out of the callback's own context struct. No result is
handed back across threads, since there's only one thread.

- `syn_pulse_list_sinks()`/`syn_pulse_list_sources()` fetch the server's
  default sink/source name first (`pa_context_get_server_info`), then
  list every sink/source, flagging whichever one matches the default.
  Monitor sources (the paired `.monitor` source every sink has, used for
  loopback/"what you hear" recording) are filtered out of the sources
  list — an "AUDIO INPUTS" list is for microphones, not sink monitors.
- Volume is stored internally by PulseAudio as a `pa_cvolume` (one value
  per channel); `syn_pulse_set_sink_volume()`/`..._source_volume()` first
  fetch the device's real channel count via a fresh by-name lookup (not
  the flattened average percentage `syn_pulse_device` caches for display)
  so a set/adjust broadcasts the same percentage to every channel rather
  than assuming stereo.
- `syn_pulse_adjust_*_volume()` (relative ± step) reads the current
  volume via the list path and calls the absolute `set_*_volume()` with
  the result — one call from the caller's side, two pulse round-trips
  underneath.

## The TUI

`syn_audio_tui_dashboard()` (`syn_audio_tui.c`) renders two stacked
panes — OUTPUTS and INPUTS — each a scrollable list with a 10-cell
volume bar, a `●` marker on the default device, and an `M` marker when
muted. `Tab` switches focus between panes, `↑`/`↓`/`j`/`k` move the
selection, `Enter` sets the selected device as default, `m` toggles
mute, `←`/`→`/`h`/`l` adjust volume ±5%, `Esc`/`q` quits. Themed from the
live SYN-OS palette exactly like
[syn-crypter](../tools/syn-crypter.md)/[syn-wifi](./wifi.md) (all three
share `syn_theme.c`, read from `~/.config/syn-os/current-theme`).

`main.c`'s interactive loop re-fetches both device lists from the server
before every redraw, so external volume changes (e.g. a scroll-wheel
adjustment via waybar's `pamixer` binding while the TUI is open) show up
on the next keypress rather than requiring a restart.

## Dependencies

`libpulse` (the PulseAudio client library — `pipewire-pulse` provides
the actual running server, exposing the compatible protocol),
`ncurses` (`ncursesw`). No `pactl` subprocess, no Qt.
