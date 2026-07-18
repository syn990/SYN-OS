# Theme Gallery

All 14 themes SYN-OS ships, grouped by `SYN_THEME_GROUP` the same way the
desktop's Themes pipe menu groups them. For the mechanism behind how a theme
switch actually reaches the desktop — the `.theme` file format, the full
`SYN_*` variable contract, and the end-to-end apply flow — see
[Theme Engine](./theme-engine.md). This page is a visual reference only.

Every entry below points at a screenshot that does not exist yet
(`../screenshots/desktop-<THEME-NAME>.png`). These are placeholders for a
future capture pass on a real running SYN-OS desktop — expect broken-image
text where each one renders until the real files are added.

## Vanilla

Original SYN-OS palettes with no external reference point.

### SYN-OS-RED (default)

Glyph: `●`

Near-black with a red tint (`SYN_BG_ALT` is `#100000`, not neutral black),
dark maroon accent (`#800000`), off-white text. The default theme every
fresh install boots into before any other theme has ever been applied.

![SYN-OS-RED desktop](../screenshots/desktop-SYN-OS-RED.png)

*Desktop with the SYN-OS-RED wallpaper, Waybar visible along its edge, and
an open `foot` terminal showing the red-on-black default palette.*

### SYN-OS-BLUE

Glyph: `❄`

Pure black background, cold saturated blue accent (`#0986d3`), white text.
The coldest-reading theme of the set.

![SYN-OS-BLUE desktop](../screenshots/desktop-SYN-OS-BLUE.png)

*Desktop with the SYN-OS-BLUE wallpaper, Waybar showing the blue accent on
workspace focus and hover states, and an open terminal.*

### SYN-OS-GREEN

Glyph: *(none — `SYN_GLYPH` is empty)*

Pure black background, saturated green accent (`#1db31d`), pale green-white
text (`#e8ffe8`). Bright and high-contrast rather than muted.

![SYN-OS-GREEN desktop](../screenshots/desktop-SYN-OS-GREEN.png)

*Desktop with the SYN-OS-GREEN wallpaper, Waybar, and an open terminal
showing the green accent against pure black.*

### SYN-OS-M141

Glyph: *(none)*

Pure black (not red-tinted like RED's near-black) with a brighter scarlet
accent (`#e00000`) — a dedicated variant built as its own theme rather than
a recolor of RED, for a specific person's own red/black request.

![SYN-OS-M141 desktop](../screenshots/desktop-SYN-OS-M141.png)

*Desktop with the SYN-OS-M141 wallpaper, Waybar showing the brighter scarlet
accent next to RED's darker maroon for comparison, and an open terminal.*

### SYN-OS-ORANGE

Glyph: *(none)*

Near-black with warm undertones (`SYN_BG_ALT` is `#1a0d00`), bright orange
accent (`#ff8800`), warm tan text (`#ffd9a3`).

![SYN-OS-ORANGE desktop](../screenshots/desktop-SYN-OS-ORANGE.png)

*Desktop with the SYN-OS-ORANGE wallpaper, Waybar, and an open terminal
showing the warm orange-on-black palette.*

### SYN-OS-PINK

Glyph: `✦`

Near-black, hot magenta-pink accent (`#ff0080`), pink-tinted border color
(`#ff5ca8` — the only theme where the border itself carries the accent hue
rather than a neutral gray).

![SYN-OS-PINK desktop](../screenshots/desktop-SYN-OS-PINK.png)

*Desktop with the SYN-OS-PINK wallpaper, Waybar, and an open terminal
showing the magenta-pink accent and matching window borders.*

### SYN-OS-PURPLE

Glyph: *(none)*

Pure black, violet accent (`#9b30d9`), pale lavender text (`#f3e8ff`).

![SYN-OS-PURPLE desktop](../screenshots/desktop-SYN-OS-PURPLE.png)

*Desktop with the SYN-OS-PURPLE wallpaper, Waybar, and an open terminal
showing the violet accent against black.*

### SYN-OS-YELLOW

Glyph: `⚡`

Near-black with warm-yellow undertones, gold accent (`#e6d000`), pale
straw-yellow text (`#f5eeb0`).

![SYN-OS-YELLOW desktop](../screenshots/desktop-SYN-OS-YELLOW.png)

*Desktop with the SYN-OS-YELLOW wallpaper, Waybar, and an open terminal
showing the gold accent against black.*

## Homage

Deliberate recreations of a specific well-known look, both sourced from
third-party Openbox `themerc` files and both requiring theme-specific
override templates to reproduce that look faithfully — see
[Theme Engine](./theme-engine.md#theme-specific-override-templates-matrix-and-win95).

### SYN-OS-MATRIX

Glyph: *(none)*

Classic green-on-black terminal look. Mint-green text (`#90ee90`), lime
accent (`#32cd32`), flat `Solid` titlebars with a thin 1px border — no
gradients, no bevels, reproducing the original "Retro 1 (Terminal)" Openbox
theme it's based on.

![SYN-OS-MATRIX desktop](../screenshots/desktop-SYN-OS-MATRIX.png)

*Desktop with the SYN-OS-MATRIX wallpaper, Waybar's accent-line drawn at the
top edge (this theme's override flips it from the shared bottom-edge
default), and an open terminal in the green-on-black palette.*

### SYN-OS-WIN95

Glyph: *(none)*

Flat, beveled Windows 95 look. Silver-gray surfaces (`#c0c0c0`), classic
titlebar blue accent (`#0a246a`) rendered as a vertical gradient into a pale
blue, black text — the only theme whose terminal palette is fully hardcoded
to the real 16-color VGA/console palette rather than derived from its own
`SYN_*` UI colors.

![SYN-OS-WIN95 desktop](../screenshots/desktop-SYN-OS-WIN95.png)

*Desktop with the SYN-OS-WIN95 wallpaper, Waybar in the silver/blue palette,
a window showing the gradient titlebar, and an open terminal in the
classic console color palette.*

## Neutral

Desaturated or monochrome palettes with no strong accent hue.

### SYN-OS-BRIGHT

Glyph: *(none)*

The one genuinely light-background theme in the "everyday" sense — near-white
background (`#f5f5f5`), dark text (`#1a1a1a`), restrained blue accent
(`#1a5fb4`).

![SYN-OS-BRIGHT desktop](../screenshots/desktop-SYN-OS-BRIGHT.png)

*Desktop with the SYN-OS-BRIGHT wallpaper, Waybar in light mode, and an open
terminal showing dark-on-light text.*

### SYN-OS-GRAPHITE

Glyph: *(none)*

Neutral dark gray with no hue lean at all — desaturated gray-blue accent
(`#8a8f98`), gray text (`#d4d4d4`). The most muted theme of the set.

![SYN-OS-GRAPHITE desktop](../screenshots/desktop-SYN-OS-GRAPHITE.png)

*Desktop with the SYN-OS-GRAPHITE wallpaper, Waybar in monochrome gray, and
an open terminal showing the neutral palette.*

### SYN-OS-LIGHT

Glyph: `◐`

Despite the name, a dark theme (`#1e1e1e` background) with a soft blue
accent (`#5c9eff`) — reads as a lighter, softer counterpart to GRAPHITE
rather than an actual light-background theme.

![SYN-OS-LIGHT desktop](../screenshots/desktop-SYN-OS-LIGHT.png)

*Desktop with the SYN-OS-LIGHT wallpaper, Waybar, and an open terminal
showing the soft blue accent against dark gray, for comparison against
GRAPHITE and BRIGHT.*

### SYN-OS-SILVER

Glyph: *(none)*

Light gray-on-gray (`#c8c8c8` background), dark slate accent (`#3a3d42`),
near-black text — the other light-background theme besides BRIGHT, but
cooler and grayer rather than near-white.

![SYN-OS-SILVER desktop](../screenshots/desktop-SYN-OS-SILVER.png)

*Desktop with the SYN-OS-SILVER wallpaper, Waybar in the gray palette, and
an open terminal, for comparison against SYN-OS-BRIGHT.*

## Related docs

- [Theme Engine](./theme-engine.md) — the full mechanism: `.theme` file
  format, `SYN_*` variable reference, template rendering, and the live
  apply flow.
- [LabWC](../labwc.md) — where the Themes menu itself lives in the desktop.
