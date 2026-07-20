# SYN-GRAPHMAP

SYN-GRAPHMAP recursively graphs a directory tree as a Graphviz diagram,
styled in the live SYN-OS theme. It is a full rewrite of an earlier
throwaway script (`syn-mapper.sh`, kept only as a lineage reference, not
part of the live tool) — the two share the idea (walk a directory, emit a
`.dot` file, render it) but not the implementation or output styling.

## Files

| File | Role |
|---|---|
| `/usr/lib/syn-os/syn-graphmap.zsh` | The real tool — walks a directory, writes a `.dot` file, renders it with `dot` |
| `/usr/lib/syn-os/syn-graphmap-quick.zsh` | menu.xml's "Quick" entry — fixed shallow depth |
| `/usr/lib/syn-os/syn-graphmap-full.zsh` | menu.xml's "Full" entry — effectively unlimited depth |
| `/usr/lib/syn-os/syn-graphmap-custom.zsh` | menu.xml's "Custom" entry — prompts for a depth via rofi |
| `syn-mapper.sh` | Predecessor script, kept for reference only — not installed, not menu-reachable |

## `syn-graphmap.zsh`

```
syn-graphmap.zsh [directory] [max-depth] [format]
```

All three arguments are optional and independently prompted for if
omitted:

- **directory** — if empty, offered via a rofi list seeded with `$PWD`,
  `$HOME`, and `$HOME/GithubProjects`.
- **max-depth** — defaults to `6` if the argument is entirely absent (not
  prompted; the three menu variants below always pass an explicit value).
- **format** — `png` or `svg`; if empty, offered via a rofi list. Any
  other value falls back to `png` with an error notice. PNG is raster
  (sharp at 100% zoom, blurs if scaled up by a viewer); SVG is vector and
  stays sharp at any zoom.

The walk itself (`generate_structure`) recurses via a zsh function, not
`find`, skipping a fixed exclude list — `.git`, `.cache`, `node_modules`,
`WORKDIR`, `.syncache` — and stopping once `depth > MAX_DEPTH`. Directories
become graph nodes with edges to their parent; files become box-shaped
nodes with dotted edges. Every node/edge color comes from the live theme's
`SYN_BG`, `SYN_ACCENT`, and `SYN_TEXT` variables (see
[Theme Engine](../theming/theme-engine.md)) — the graph is themed the same
way the rest of the desktop is, not hardcoded to any one palette.

Output lands in `~/Pictures/SYN-GRAPHMAP/`, matching the convention
[screenshot and recording tools](./screenshot-and-recording.md) use for
`~/Pictures/Screenshots`:

- `<dirname>.dot` — the generated Graphviz source
- `<dirname>-dot.<format>` — the rendered output

PNG rendering passes `-Gdpi=300` to `dot` (resolution only, doesn't affect
layout); SVG rendering has no DPI flag since vector output has no fixed
resolution to set.

## Quick / Full / Custom

The three menu.xml entries under **SYN-GRAPHMAP (graph a directory tree)**
differ only in what `max-depth` they pass — directory and format are still
always prompted by `syn-graphmap.zsh` itself, since all three variants call
it with those two arguments left empty:

| Variant | Script | Depth passed | Prompts for depth? |
|---|---|---|---|
| Quick (shallow scan) | `syn-graphmap-quick.zsh` | `2` | No — fixed |
| Full (deep scan) | `syn-graphmap-full.zsh` | `999` | No — fixed (effectively unlimited) |
| Custom (enter a depth) | `syn-graphmap-custom.zsh` | rofi input, default `6` | Yes |

All three run `syn-graphmap.zsh` inside `syn_popup::run` (the same
self-closing popup-terminal wrapper used by
[SYN-SHARE](./syn-share.md)/[SYN-CRYPTER](./syn-crypter.md)/[SYN-REDSHIRT](./syn-redshirt.md)),
so the window closes itself once rendering finishes rather than sitting
open.

## Dependency

`dot`, from the `graphviz` package, does the actual layout and rendering
for every variant — nothing in SYN-GRAPHMAP implements its own graph
layout. If `graphviz` isn't installed, `dot` simply isn't found and the
script fails at the final render step; `syn-graphmap.zsh` does not
`ensure_pkg`-style auto-install it the way [SYN-SHARE](./syn-share.md)'s
library does for its own protocol packages. See [Packages](../packages.md)
for whether `graphviz` ships by default.

## Desktop integration

**Applications > SYN-OS Tools > SYN-GRAPHMAP (graph a directory tree)** in
the [labwc](../labwc.md) root menu is a submenu with the three Quick/Full/
Custom entries above, each launched directly (no `foot` wrapper at the
menu level — `syn_popup::run` supplies the terminal).

## Predecessor: `syn-mapper.sh`

`syn-mapper.sh` is an older, standalone script — plain `#!/bin/zsh`, not
integrated with SYN-OS's theme system, popup library, or menu. Run as
`syn-mapper <directory>`, it hardcodes a black/red color scheme
(`bgcolor="#000000"`, `color="#8B0000"`, red edges) rather than reading the
live theme, walks the tree with zsh's own recursive glob (`${SCAN_ROOT}/**/*(Don)`)
instead of a custom recursive function, and renders with **both** `dot` and
`fdp` layouts to PNG and SVG (four output files total) into
`./syn-mapper-output/` relative to the current directory, rather than the
fixed `~/Pictures/SYN-GRAPHMAP/` `syn-graphmap.zsh` uses. It checks its own
dependencies (`dot`, `fdp`, `realpath`) up front and prompts before
overwriting a prior run's output directory. It is not installed to
`/usr/lib/syn-os/`, has no menu.xml entry, and is not part of the live
desktop — it exists in the tree purely as the design this rewrite started
from.
