# syn-lag

Measures how long an app takes to show a keypress, from the moment the
key goes in to the frame that shows it. A number per app instead of "it
feels laggy". **Not built yet:** `src/main.c` knows the commands and says
so.

## Commands

```
syn-lag              the focused window: 50 presses, median and worst
syn-lag -n N         N presses
syn-lag --app CMD    start CMD, wait for its window, measure it, close it
syn-lag --suite      foot, syn-shell, surf and Falkon in turn, one table
```

## How it measures

1. Capture the app's window with `ext-image-copy-capture-v1`, picking the
   window from `ext-foreign-toplevel-list-v1`. syn-relay's screen host
   already uses both, and their generated code is in
   `syn-relay-src/protocol/`.
2. Press a key that prints a character into a text field, through
   `zwp_virtual_keyboard_v1`. A virtual keyboard has to upload an XKB
   keymap first, which is why syn-relay doesn't forward the keyboard yet.
   This is shared work: write it once, and relay uses it too.
3. Record when the key went in, capture frames until one differs from
   the frame before the key, and take that frame's time from the
   capture's `presentation_time` event.
4. Backspace, wait for it to settle, repeat.

It measures to the compositor's frame, not to light leaving the screen.
The display's own delay comes on top and is the same for every app, so
comparisons between apps still hold.

## Needs

- labwc offering `zwp_virtual_keyboard_manager_v1` and
  `ext_image_copy_capture_manager_v1`. wlroots implements both; check
  that labwc advertises them with `wayland-info`.
- A text field with focus in the app being measured.

## Files it will have

```
src/main.c
src/syn_lag_wl.c/.h        registry, outputs, toplevels
src/syn_lag_keyboard.c/.h  keymap upload and key presses (xkbcommon, as syn-uplink-dialpad uses)
src/syn_lag_capture.c/.h   frames and their presentation times
src/syn_lag_measure.c/.h   the press-and-watch loop, median and worst
protocol/                  virtual-keyboard-unstable-v1 generated here; the capture
                           protocols reused from syn-relay-src/protocol/
```

## Build order

1. Capture a window and print each frame's presentation time.
2. The virtual keyboard with a keymap, written so syn-relay can reuse it.
3. The measuring loop.
4. `--app` and `--suite`.

## Open questions

- **Spotting the frame that shows the key.** Comparing whole frames is
  simple but slow for large windows. Comparing only the area around the
  text cursor would be faster, if the cursor can be found reliably.
