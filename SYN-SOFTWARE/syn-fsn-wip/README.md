# syn-fsn

The file system as a 3D picture, after SGI's fsn (the one in *Jurassic
Park*). It's a viewer, not a game: you turn it, zoom into it and click
things. Folders are platforms, files are blocks standing on them, height
shows size, color shows type or age, and what changed recently glows.
"Where has my disk gone" and "what changed today" are answered at a
glance. **Not built yet:** `src/main.c` knows the commands and says so.

## Commands

```
syn-fsn [DIR]                show DIR (your home folder if none)
syn-fsn --by size|age|type   what height and color mean (default: height is size, color is type)
syn-fsn --dump [DIR]         print the layout as text, no window
```

## Using it

- Drag to turn the view around the folder in focus; scroll to zoom.
- Click a folder: the view glides to it and it becomes the focus.
- Click a file: its path, size, owner and dates show in a panel.
- Hover anything for its name.
- Backspace goes up to the parent folder; the arrow keys move between
  folders side by side.
- Enter opens the selected file with `xdg-open`.
- `1`, `2` and `3` switch between size, age and type, the same as `--by`.

## How it's built

**The layout** is plain C with no graphics. It walks the tree (`openat`
and `fstatat`, never following a symlink out of it) into nodes with size,
mtime, type and child count, then places them:

- each folder is a platform, with its subfolders on smaller platforms in
  a row behind it, joined by lines
- files are blocks in a grid on their folder's platform, height from
  size on a log scale

Big trees load lazily: a folder's children are read when it comes into
focus or near it. `--dump` prints the layout, so it can be built and
checked before anything is drawn.

**The renderer** is its own, small, and does only what this needs:

- SDL2 for the window and input, OpenGL 3.3 through libepoxy
- every block is one instanced cube, flat-shaded; lines for the joins
- labels and the info panel through SDL2_ttf, in Terminus
- clicking casts a ray from the mouse and tests it against the blocks'
  boxes
- matrix math (perspective, look-at, multiply) is a small header written
  here, not a library

Colors come from the active theme: `SYN_BG` behind everything,
`SYN_PANEL` for platforms, `SYN_ACCENT` for the focus and selection,
`SYN_TEXT` for labels. Age runs from `SYN_ACCENT_DIM` (old) to
`SYN_ACCENT` (new). Type uses a short fixed set of hues, tinted toward
the theme so they sit with it.

**Later:**

- A syn-autopsy `diff` loaded on top, so files that changed since the
  baseline show in `SYN_URGENT`.
- Another machine's tree beside yours over SYN-RELAY, once relay has a
  file-listing channel and authentication.

## Files it will have

```
src/main.c
src/syn_fsn_tree.c/.h     walking, nodes, lazy loading
src/syn_fsn_layout.c/.h   nodes -> positions and sizes
src/syn_fsn_dump.c        --dump
src/syn_fsn_math.h        vectors and 4x4 matrices
src/syn_fsn_gl.c/.h       shaders, the instanced cube, lines
src/syn_fsn_text.c/.h     labels and the info panel
src/syn_fsn_view.c/.h     the camera: turning, zooming, gliding to a focus
src/syn_fsn_pick.c/.h     the mouse ray against blocks
```

Plus `syn_theme.c` from syn-crypter-src for the palette.

## Build order

1. The tree and the layout, with `--dump`.
2. A window: the layout as plain boxes, with the turning camera.
3. Picking, focus and the info panel.
4. Theme, labels and `--by`.
5. Lazy loading for big trees.
6. The autopsy overlay, then other machines.

## Open questions

- **Hidden files.** Probably a key to toggle them, off to start.
- **Huge folders.** With thousands of files, one block per file stops
  being readable. Past some count, files could merge into one block per
  type and split apart as you zoom in.
