# Screenshots and Screen Recording

SYN-OS ships two capture scripts, both living in `/usr/lib/syn-os/`:
`screenshot.zsh` and `screen-recorder.zsh`. Both are Wayland-native (`grim` /
`slurp` / `wf-recorder`), both prompt for an output directory with a themed
rofi picker before doing anything, and both report completion with a toast
via `notify-send` — see [notifications.md](./notifications.md) for how that
toast pipeline works. Recording additionally exposes its running/stopped
state to waybar's `custom/recording` module through a PID file.

## Screenshot: `screenshot.zsh`

Path: `/usr/lib/syn-os/screenshot.zsh`

```
screenshot.zsh [full|region]
```

`$1` selects the mode; it defaults to `full` if omitted.

1. Sources `syn-theme-lib.zsh` and `syn-picker-lib.zsh`, then calls
   `syn_theme_load` so the save-location picker is drawn in the live theme's
   colors.
2. Prompts for an output directory with `syn_pick::rofi`, offering five
   presets on stdin:
   - `$HOME/Pictures/Screenshots` (the default)
   - `$HOME`
   - `$HOME/Videos`
   - `$HOME/Desktop`
   - `$HOME/Downloads`

   If the picker is dismissed with no selection, it falls back to
   `$HOME/Pictures/Screenshots`. A leading `~` typed by hand is expanded
   manually (`OUT_DIR="${OUT_DIR/#\~/$HOME}"`) since the value comes back as
   a literal string, not through shell tilde expansion. The chosen directory
   is created with `mkdir -p` if it doesn't already exist.
3. Mode dispatch:
   - `full` — no region argument is built; `grim` captures the whole output
     with no `-g` flag.
   - `region` — runs `slurp` to get an interactive selection. If the user
     cancels `slurp` (empty output), the script exits `0` immediately with
     no screenshot taken. Otherwise the selection string is passed to `grim`
     as `-g "$region"`.
4. Output filename: `$OUT_DIR/$(date +%F_%H-%M-%S).png` — e.g.
   `2026-07-18_14-32-05.png`. `grim` writes directly to that path.
5. On completion: `notify-send "Screenshot" "Saved -> $out_file"`.

There is no separate "copy to clipboard" step — the file lands on disk at
the chosen path and the toast names the exact path.

## Screen recording: `screen-recorder.zsh`

Path: `/usr/lib/syn-os/screen-recorder.zsh`

```
screen-recorder.zsh [full|region] [audio-device]
```

Wraps **wf-recorder**. `$1` selects the mode (default `full`); `$2`, if
given, is an audio device name to pass through for audio capture (no audio
by default) — e.g.:

```
screen-recorder.zsh full alsa_output.pci-0000_00_1f.3.analog-stereo.monitor
```

### Start/stop toggle — the PID-file pattern

The script is a single entry point that toggles based on whether a
recording is already running:

```
PIDFILE="$XDG_RUNTIME_DIR/syn-screen-recorder.pid"
```

- **If `$PIDFILE` exists and its PID is alive** (`kill -0 "$pid"` succeeds):
  this call is a **stop**. It sends `SIGINT` to the recorder process
  (`kill -INT "$pid"` — wf-recorder finalizes the output file cleanly on
  `SIGINT` rather than leaving a corrupt/unclosed mp4), removes the PID
  file, toasts `"Screen recording" "Stopped"`, and exits `0`. None of the
  theme/picker/mode logic below runs on a stop call.
- **Otherwise** (no PID file, or the PID it names is dead): this call is a
  **start**, and the rest of the script runs.

This means the same command toggles both directions — `screen-recorder.zsh
full` run twice in a row starts, then stops, regardless of whether the
second call still says `full`. The mode argument only matters on the call
that actually starts a new recording.

### Start flow

1. Sources `syn-theme-lib.zsh` / `syn-picker-lib.zsh`, calls
   `syn_theme_load`.
2. Prompts for a save directory via `syn_pick::rofi`, same pattern as
   `screenshot.zsh` but with different presets: `$HOME/Videos` (default),
   `$HOME`, `$HOME/Desktop`, `$HOME/Downloads`. Same tilde-expansion and
   `mkdir -p` handling.
3. Mode dispatch:
   - `region` — runs `slurp` for an interactive area selection; empty
     selection (cancelled) exits `0` with nothing started. The selection is
     passed to `wf-recorder` as `-g "$region"`.
   - `full` (default) — enumerates connected outputs with `wlr-randr`,
     parsed by an `awk` script that tracks each output block (`name`,
     `Enabled:`, `Make:`, `Model:`) and, for the block containing
     `current)` (the current-mode line), prints one line per **enabled**
     output as `<port-name>\t<human label> — <mode>`, e.g.:
     ```
     DP-2	LG ULTRAWIDE — 3440x1440
     ```
     If there's more than one enabled output, the choices are shown in
     `syn_pick::rofi` with `-display-columns 2 -display-column-separator
     '\t'` (so only the human label column is visible), and the chosen
     line's port name (column 1) is extracted with `cut -f1` and passed to
     `wf-recorder` as `-o "$chosen"`. If only one (or zero) output is
     enabled, no picker is shown and `wf-recorder` records the default
     output with no `-o` flag. Cancelling the picker exits `0` with nothing
     started.
4. Output filename: `$OUT_DIR/$(date +%F_%H-%M-%S).mp4`.
5. Audio: if `$2` was passed, `-a "$2"` is added to the `wf-recorder`
   invocation; otherwise recording is silent.
6. `wf-recorder` is launched in the background:
   ```
   wf-recorder "${audio_args[@]}" "${geometry_args[@]}" \
     -c libx264 -x yuv420p -p preset=fast -p crf=23 \
     -f "$out_file" >"$XDG_RUNTIME_DIR/syn-screen-recorder.log" 2>&1 &
   ```
   `libx264` at `preset=fast` / `crf=23` is a deliberate choice — `fast`
   avoids x264 profile/level ceilings that bite at high output resolutions
   (e.g. ultrawide monitors), which stricter presets can hit. Its stdout and
   stderr are redirected to `$XDG_RUNTIME_DIR/syn-screen-recorder.log` for
   post-mortem debugging if a recording fails silently.
7. `echo $! > "$PIDFILE"` records the backgrounded `wf-recorder` PID.
8. Toast: `notify-send "Screen recording" "Started -> $out_file"`.

### Files involved

| Path | Purpose |
|---|---|
| `$XDG_RUNTIME_DIR/syn-screen-recorder.pid` | PID of the running `wf-recorder` process; presence + liveness is the toggle state |
| `$XDG_RUNTIME_DIR/syn-screen-recorder.log` | `wf-recorder` stdout/stderr, for debugging failed recordings |
| `$OUT_DIR/<timestamp>.mp4` | the recording itself |

## Keybinds

Defined in `~/.config/labwc/rc.xml`:

| Key | Action |
|---|---|
| `Super+P` (`W-p`) | `/usr/lib/syn-os/screenshot.zsh region` |
| `Super+Shift+P` (`W-S-p`) | `/usr/lib/syn-os/screen-recorder.zsh full` |

Note the asymmetry: the direct keybinds cover **region screenshot** and
**full-screen recording toggle** specifically — the other two combinations
(full-screen screenshot, region recording) are menu-only, not bound to a
key. See [../labwc.md](../labwc.md) for the complete keybind reference.

## Capture menu

The root menu's `menu.xml` exposes all four screenshot/recording actions
under a `Capture` submenu, alongside the direct keybinds:

| Label | Command |
|---|---|
| Screenshot - full screen | `/usr/lib/syn-os/screenshot.zsh full` |
| Screenshot - select area | `/usr/lib/syn-os/screenshot.zsh region` |
| Record - full screen | `/usr/lib/syn-os/screen-recorder.zsh full` |
| Record - select area / Stop | `/usr/lib/syn-os/screen-recorder.zsh region` |

The last entry's label calls out the toggle behavior directly: since
`screen-recorder.zsh` stops any in-progress recording regardless of the
mode argument passed, clicking either "Record" entry again while a
recording is running stops it — "Record - select area / Stop" is labeled
that way because it's the entry most likely to be clicked as a stop action
once a region recording is already going.

`Display & Screens` (output on/off, layout) is deliberately not part of
this menu — it's configuration, not a capture action, and lives under
`Preferences` instead.

## waybar `custom/recording` module

Defined in `~/.config/waybar/config.jsonc`, placed first in
`modules-right`:

```jsonc
"custom/recording": {
  "format": "{}",
  "exec": "PIDFILE=\"$XDG_RUNTIME_DIR/syn-screen-recorder.pid\"; [ -f \"$PIDFILE\" ] && kill -0 \"$(cat \"$PIDFILE\")\" 2>/dev/null && echo 'Recording' || echo ''",
  "interval": 2,
  "tooltip-format": "Recording — click to stop",
  "on-click": "/usr/lib/syn-os/screen-recorder.zsh full"
}
```

- **`exec`** polls the exact same PID file `screen-recorder.zsh` uses
  (`$XDG_RUNTIME_DIR/syn-screen-recorder.pid`), checking both that the file
  exists and that the PID inside it is still alive (`kill -0`). It prints
  `Recording` when a capture is active, an empty string otherwise — so the
  module renders nothing in the bar when idle and shows the word
  `Recording` when a capture is running.
- **`interval: 2`** — polled every 2 seconds, matching the poll interval
  most other live-state waybar modules use in this config (e.g. `network`,
  `cpu`).
- **`on-click`** runs `screen-recorder.zsh full` — because of the toggle
  logic described above, clicking the module while a recording is active
  stops it (the `full` argument is irrelevant on a stop call); clicking it
  while idle starts a new full-screen recording.

This is the one waybar module documented as following the PID-file pattern
— see [../waybar.md](../waybar.md) for the rest of the bar's modules.

## Dependencies

`grim`, `slurp`, `wf-recorder`, `wlr-randr` (for multi-output selection in
full-recording mode), and `rofi` (via `syn-picker-lib.zsh`) must all be
present. All are part of the standard SYN-OS package set.
