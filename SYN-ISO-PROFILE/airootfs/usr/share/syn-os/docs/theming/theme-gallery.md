# Theme Gallery

All 63 themes SYN-OS ships, grouped the same way the desktop's Themes pipe
menu groups them: Dark or Light mode first, then split into 5 structural
families — Vanilla, Flatline, Slab, Halo, Bevel. For the mechanism behind
how a theme switch actually reaches the desktop — the `.theme` file format,
the full `SYN_*` variable contract, what each family structurally means, and
the end-to-end apply flow — see [Theme Engine](./theme-engine.md). This page
is a visual reference only.

Every entry below points at a screenshot that does not exist yet
(`../screenshots/desktop-<THEME-NAME>.png`). These are placeholders for a
future capture pass on a real running SYN-OS desktop — expect broken-image
text where each one renders until the real files are added.

## Dark

### Vanilla

Original SYN-OS palettes with no external reference point: flat color
fields, no borders beyond a hairline, no gradients or shadows.

#### SYN-OS-RED (default)

Glyph: `●`

Near-black with a red tint (`SYN_BG_ALT` is `#100000`, not neutral black),
dark maroon accent (`#800000`), off-white text (`#f8f8f2`). The default
theme every fresh install boots into before any other theme has ever been
applied.

![SYN-OS-RED desktop](../screenshots/desktop-SYN-OS-RED.png)

*Desktop with the SYN-OS-RED wallpaper, Waybar visible along its edge, and
an open `foot` terminal showing the red-on-black default palette.*

#### SYN-OS-BLUE

Glyph: `❄`

Pure black background (`#000000`), cold saturated blue accent (`#0986d3`),
white text (`#ffffff`). The coldest-reading theme of the set.

![SYN-OS-BLUE desktop](../screenshots/desktop-SYN-OS-BLUE.png)

*Desktop with the SYN-OS-BLUE wallpaper, Waybar showing the blue accent on
workspace focus and hover states, and an open terminal.*

#### SYN-OS-GREEN

Glyph: *(none — `SYN_GLYPH` is empty)*

Pure black background (`#000000`), saturated green accent (`#1db31d`), pale
green-white text (`#e8ffe8`). Bright and high-contrast rather than muted.

![SYN-OS-GREEN desktop](../screenshots/desktop-SYN-OS-GREEN.png)

*Desktop with the SYN-OS-GREEN wallpaper, Waybar, and an open terminal
showing the green accent against pure black.*

#### SYN-OS-M141

Glyph: *(none)*

Pure black (`#000000`, not red-tinted like RED's near-black) with a
brighter scarlet accent (`#e00000`) and white text (`#ffffff`) — a
dedicated variant built as its own theme rather than a recolor of RED, for
a specific person's own red/black request.

![SYN-OS-M141 desktop](../screenshots/desktop-SYN-OS-M141.png)

*Desktop with the SYN-OS-M141 wallpaper, Waybar showing the brighter scarlet
accent next to RED's darker maroon for comparison, and an open terminal.*

#### SYN-OS-ORANGE

Glyph: *(none)*

Near-black with warm undertones (`SYN_BG_ALT` is `#1a0d00`), bright orange
accent (`#ff8800`), warm tan text (`#ffd9a3`).

![SYN-OS-ORANGE desktop](../screenshots/desktop-SYN-OS-ORANGE.png)

*Desktop with the SYN-OS-ORANGE wallpaper, Waybar, and an open terminal
showing the warm orange-on-black palette.*

#### SYN-OS-PINK

Glyph: `✦`

Near-black (`#0a0a0a`), hot magenta-pink accent (`#ff0080`), pale pink-white
text (`#ffe0f0`).

![SYN-OS-PINK desktop](../screenshots/desktop-SYN-OS-PINK.png)

*Desktop with the SYN-OS-PINK wallpaper, Waybar, and an open terminal
showing the magenta-pink accent against near-black.*

#### SYN-OS-PURPLE

Glyph: *(none)*

Pure black background (`#000000`), violet accent (`#9b30d9`), pale lavender
text (`#f3e8ff`).

![SYN-OS-PURPLE desktop](../screenshots/desktop-SYN-OS-PURPLE.png)

*Desktop with the SYN-OS-PURPLE wallpaper, Waybar, and an open terminal
showing the violet accent against black.*

#### SYN-OS-YELLOW

Glyph: `⚡`

Near-black with warm-yellow undertones (`#0a0a00`), gold accent (`#e6d000`),
pale straw-yellow text (`#f5eeb0`).

![SYN-OS-YELLOW desktop](../screenshots/desktop-SYN-OS-YELLOW.png)

*Desktop with the SYN-OS-YELLOW wallpaper, Waybar, and an open terminal
showing the gold accent against black.*

#### SYN-OS-GRAPHITE

Glyph: *(none)*

Neutral dark gray with no hue lean at all (`#242424` background) —
desaturated gray-blue accent (`#8a8f98`), gray text (`#d4d4d4`). The most
muted theme of the set. Previously grouped under a "Neutral" category;
reclassified as Vanilla-dark since it's structurally plain flat color
fields with no distinguishing border or shadow treatment. Its colors are
unchanged.

![SYN-OS-GRAPHITE desktop](../screenshots/desktop-SYN-OS-GRAPHITE.png)

*Desktop with the SYN-OS-GRAPHITE wallpaper, Waybar in monochrome gray, and
an open terminal showing the neutral palette.*

#### SYN-OS-LIGHT

Glyph: `◐`

Despite the name, a dark theme (`#1e1e1e` background) with a soft blue
accent (`#5c9eff`) and light gray text (`#e0e0e0`) — reads as a lighter,
softer counterpart to GRAPHITE rather than an actual light-background
theme. Previously grouped under "Neutral"; reclassified as Vanilla-dark
for the same structural reason as GRAPHITE. Its colors are unchanged.

![SYN-OS-LIGHT desktop](../screenshots/desktop-SYN-OS-LIGHT.png)

*Desktop with the SYN-OS-LIGHT wallpaper, Waybar, and an open terminal
showing the soft blue accent against dark gray, for comparison against
GRAPHITE.*

### Flatline

Zero border-radius, zero shadow, hairline borders only. Waybar modules have
no panel background at all — just text on the bar's own background. The
most minimal family.

#### SYN-OS-MATRIX

Glyph: *(none)*

Classic green-on-black terminal look. Pure black background (`#000000`),
lime accent (`#32cd32`), mint-green text (`#90ee90`), flat `Solid`
titlebars with a thin 1px border — no gradients, no bevels, reproducing the
original "Retro 1 (Terminal)" Openbox theme it's based on. Previously its
own "Homage" category; now classified structurally under Flatline, since
its flat, hairline-bordered, no-panel-background look matches the family
exactly. It keeps its own exact-name override templates for LabWC and
Waybar regardless (see
[Theme Engine](./theme-engine.md#theme-specific-override-templates-matrix-and-win95)).

![SYN-OS-MATRIX desktop](../screenshots/desktop-SYN-OS-MATRIX.png)

*Desktop with the SYN-OS-MATRIX wallpaper, Waybar's accent-line drawn at the
top edge (this theme's override flips it from the shared bottom-edge
default), and an open terminal in the green-on-black palette.*

#### SYN-OS-FLATLINE-CYAN

Glyph: `▪`

Pure black background (`#000000`), bright cyan accent (`#00e5e5`), pale cyan
text (`#b3f5f5`).

![SYN-OS-FLATLINE-CYAN desktop](../screenshots/desktop-SYN-OS-FLATLINE-CYAN.png)

*Desktop with the SYN-OS-FLATLINE-CYAN wallpaper, Waybar modules rendered
as bare text with no panel background, and an open terminal in the
cyan-on-black palette.*

#### SYN-OS-FLATLINE-MAGENTA

Glyph: `▪`

Pure black background (`#000000`), hot magenta accent (`#ff33bb`), pale
pink text (`#ffd9f0`).

![SYN-OS-FLATLINE-MAGENTA desktop](../screenshots/desktop-SYN-OS-FLATLINE-MAGENTA.png)

*Desktop with the SYN-OS-FLATLINE-MAGENTA wallpaper, Waybar modules with no
panel background, and an open terminal in the magenta-on-black palette.*

#### SYN-OS-FLATLINE-LIME

Glyph: `▪`

Pure black background (`#000000`), saturated lime accent (`#7fff00`), pale
yellow-green text (`#e0ffb3`).

![SYN-OS-FLATLINE-LIME desktop](../screenshots/desktop-SYN-OS-FLATLINE-LIME.png)

*Desktop with the SYN-OS-FLATLINE-LIME wallpaper, hairline-bordered windows,
and an open terminal in the lime-on-black palette.*

#### SYN-OS-FLATLINE-GOLD

Glyph: `▪`

Pure black background (`#000000`), rich gold accent (`#ffcc00`), pale cream
text (`#fff2cc`).

![SYN-OS-FLATLINE-GOLD desktop](../screenshots/desktop-SYN-OS-FLATLINE-GOLD.png)

*Desktop with the SYN-OS-FLATLINE-GOLD wallpaper, Waybar's bare-text modules,
and an open terminal in the gold-on-black palette.*

#### SYN-OS-FLATLINE-ICE

Glyph: `❄`

Pure black background (`#000000`), pale sky-blue accent (`#66ccff`), very
pale blue text (`#e0f5ff`).

![SYN-OS-FLATLINE-ICE desktop](../screenshots/desktop-SYN-OS-FLATLINE-ICE.png)

*Desktop with the SYN-OS-FLATLINE-ICE wallpaper, hairline window borders,
and an open terminal in the ice-blue-on-black palette.*

#### SYN-OS-FLATLINE-CRIMSON

Glyph: `▪`

Pure black background (`#000000`), bright crimson-pink accent (`#ff4d66`),
pale red-pink text (`#ffd9d9`).

![SYN-OS-FLATLINE-CRIMSON desktop](../screenshots/desktop-SYN-OS-FLATLINE-CRIMSON.png)

*Desktop with the SYN-OS-FLATLINE-CRIMSON wallpaper, Waybar's no-background
modules, and an open terminal in the crimson-on-black palette.*

#### SYN-OS-FLATLINE-TEAL

Glyph: `▪`

Pure black background (`#000000`), bright teal accent (`#00d9b3`), pale
mint text (`#c2fff5`).

![SYN-OS-FLATLINE-TEAL desktop](../screenshots/desktop-SYN-OS-FLATLINE-TEAL.png)

*Desktop with the SYN-OS-FLATLINE-TEAL wallpaper, hairline-bordered windows,
and an open terminal in the teal-on-black palette.*

#### SYN-OS-FLATLINE-SLATE

Glyph: `▪`

Pure black background (`#000000`), desaturated blue-gray accent (`#8fa8bd`),
pale gray-blue text (`#dfe6ec`). The most muted Flatline palette.

![SYN-OS-FLATLINE-SLATE desktop](../screenshots/desktop-SYN-OS-FLATLINE-SLATE.png)

*Desktop with the SYN-OS-FLATLINE-SLATE wallpaper, Waybar's bare-text
modules, and an open terminal in the slate-on-black palette.*

### Slab

Thick (6px) square-cornered borders, chunky bordered/margined Waybar module
blocks. The most maximal family.

#### SYN-OS-SLAB-AMBER

Glyph: `■`

Near-black with warm undertones (`#0a0805`), bright amber accent
(`#ffb833`), pale cream text (`#fff0d9`).

![SYN-OS-SLAB-AMBER desktop](../screenshots/desktop-SYN-OS-SLAB-AMBER.png)

*Desktop with the SYN-OS-SLAB-AMBER wallpaper, Waybar's chunky bordered
module blocks, and an open terminal in the amber-on-black palette.*

#### SYN-OS-SLAB-CRIMSON

Glyph: `■`

Near-black with a red tint (`#0a0505`), bright red accent (`#ff6666`), pale
red-white text (`#ffe0e0`).

![SYN-OS-SLAB-CRIMSON desktop](../screenshots/desktop-SYN-OS-SLAB-CRIMSON.png)

*Desktop with the SYN-OS-SLAB-CRIMSON wallpaper, thick square-cornered
window borders, and an open terminal in the crimson-on-black palette.*

#### SYN-OS-SLAB-FOREST

Glyph: `■`

Near-black with a green tint (`#050a05`), bright green accent (`#4dff66`),
pale green-white text (`#e0ffe0`).

![SYN-OS-SLAB-FOREST desktop](../screenshots/desktop-SYN-OS-SLAB-FOREST.png)

*Desktop with the SYN-OS-SLAB-FOREST wallpaper, Waybar's chunky module
blocks, and an open terminal in the forest-green-on-black palette.*

#### SYN-OS-SLAB-COBALT

Glyph: `■`

Near-black with a blue tint (`#05080a`), bright sky-blue accent (`#5cabff`),
pale blue-white text (`#dceeff`).

![SYN-OS-SLAB-COBALT desktop](../screenshots/desktop-SYN-OS-SLAB-COBALT.png)

*Desktop with the SYN-OS-SLAB-COBALT wallpaper, thick bordered windows, and
an open terminal in the cobalt-on-black palette.*

#### SYN-OS-SLAB-VIOLET

Glyph: `■`

Near-black with a violet tint (`#08050a`), bright violet accent (`#c17aff`),
pale lavender text (`#f0e0ff`).

![SYN-OS-SLAB-VIOLET desktop](../screenshots/desktop-SYN-OS-SLAB-VIOLET.png)

*Desktop with the SYN-OS-SLAB-VIOLET wallpaper, Waybar's margined module
blocks, and an open terminal in the violet-on-black palette.*

#### SYN-OS-SLAB-BRASS

Glyph: `■`

Near-black with warm undertones (`#0a0805`), muted brass accent (`#eec266`),
pale cream text (`#fff0d9`).

![SYN-OS-SLAB-BRASS desktop](../screenshots/desktop-SYN-OS-SLAB-BRASS.png)

*Desktop with the SYN-OS-SLAB-BRASS wallpaper, thick square-cornered
borders, and an open terminal in the brass-on-black palette.*

#### SYN-OS-SLAB-ROSE

Glyph: `■`

Near-black with a pink tint (`#0a0508`), bright rose-pink accent (`#ff85ad`),
pale pink text (`#ffe0ec`).

![SYN-OS-SLAB-ROSE desktop](../screenshots/desktop-SYN-OS-SLAB-ROSE.png)

*Desktop with the SYN-OS-SLAB-ROSE wallpaper, Waybar's chunky module blocks,
and an open terminal in the rose-on-black palette.*

#### SYN-OS-SLAB-MONO

Glyph: `■`

Near-black (`#0a0a0a`), light gray accent (`#c4c4c4`), near-white text
(`#f0f0f0`) — no hue at all, the Slab family's monochrome entry.

![SYN-OS-SLAB-MONO desktop](../screenshots/desktop-SYN-OS-SLAB-MONO.png)

*Desktop with the SYN-OS-SLAB-MONO wallpaper, thick gray borders, and an
open terminal in the grayscale palette.*

### Halo

Glow/outline styling: accent-colored borders with no fill (backgrounds match
`SYN_BG` exactly), plus a box-shadow glow on the active Waybar taskbar
button.

#### SYN-OS-HALO-VIOLET

Glyph: `◈`

Pure black background (`#000000`), bright violet accent (`#b366ff`), pale
lavender text (`#e8d9ff`).

![SYN-OS-HALO-VIOLET desktop](../screenshots/desktop-SYN-OS-HALO-VIOLET.png)

*Desktop with the SYN-OS-HALO-VIOLET wallpaper, violet glow-outlined window
borders with no fill, and an open terminal in the violet-on-black palette.*

#### SYN-OS-HALO-CYAN

Glyph: `◈`

Pure black background (`#000000`), bright cyan accent (`#00e5cc`), pale
mint text (`#c2fff5`).

![SYN-OS-HALO-CYAN desktop](../screenshots/desktop-SYN-OS-HALO-CYAN.png)

*Desktop with the SYN-OS-HALO-CYAN wallpaper, cyan glow-outlined borders,
Waybar's active-button glow, and an open terminal.*

#### SYN-OS-HALO-AMBER

Glyph: `◈`

Pure black background (`#000000`), bright amber accent (`#ffbb00`), pale
cream text (`#fff2cc`).

![SYN-OS-HALO-AMBER desktop](../screenshots/desktop-SYN-OS-HALO-AMBER.png)

*Desktop with the SYN-OS-HALO-AMBER wallpaper, amber glow-outlined window
borders, and an open terminal in the amber-on-black palette.*

#### SYN-OS-HALO-CRIMSON

Glyph: `◈`

Pure black background (`#000000`), bright crimson-red accent (`#ff3355`),
pale red-pink text (`#ffd9d9`).

![SYN-OS-HALO-CRIMSON desktop](../screenshots/desktop-SYN-OS-HALO-CRIMSON.png)

*Desktop with the SYN-OS-HALO-CRIMSON wallpaper, crimson glow-outlined
borders, and an open terminal in the crimson-on-black palette.*

#### SYN-OS-HALO-LIME

Glyph: `◈`

Pure black background (`#000000`), saturated lime accent (`#99ff33`), pale
yellow-green text (`#e8ffcc`).

![SYN-OS-HALO-LIME desktop](../screenshots/desktop-SYN-OS-HALO-LIME.png)

*Desktop with the SYN-OS-HALO-LIME wallpaper, lime glow-outlined window
borders, and an open terminal in the lime-on-black palette.*

#### SYN-OS-HALO-ROSE

Glyph: `◈`

Pure black background (`#000000`), bright rose-pink accent (`#ff4db8`),
pale pink text (`#ffe0f0`).

![SYN-OS-HALO-ROSE desktop](../screenshots/desktop-SYN-OS-HALO-ROSE.png)

*Desktop with the SYN-OS-HALO-ROSE wallpaper, rose glow-outlined borders,
and an open terminal in the rose-on-black palette.*

#### SYN-OS-HALO-ICE

Glyph: `❄`

Pure black background (`#000000`), pale ice-blue accent (`#4db8ff`), very
pale blue text (`#dcf2ff`).

![SYN-OS-HALO-ICE desktop](../screenshots/desktop-SYN-OS-HALO-ICE.png)

*Desktop with the SYN-OS-HALO-ICE wallpaper, ice-blue glow-outlined window
borders, and an open terminal in the ice-blue-on-black palette.*

#### SYN-OS-HALO-GOLD

Glyph: `◈`

Pure black background (`#000000`), rich gold accent (`#ffd633`), pale cream
text (`#fff5cc`).

![SYN-OS-HALO-GOLD desktop](../screenshots/desktop-SYN-OS-HALO-GOLD.png)

*Desktop with the SYN-OS-HALO-GOLD wallpaper, gold glow-outlined borders,
Waybar's active-button glow, and an open terminal.*

### Bevel

`Gradient Vertical` titlebars and buttons, `linear-gradient()` Waybar module
backgrounds, a drop shadow on the bar itself. The most skeuomorphic family.

#### SYN-OS-WIN95

Glyph: *(none)*

Flat, beveled Windows 95 look. Silver-gray surfaces (`#c0c0c0`), classic
titlebar blue accent (`#0a246a`) rendered as a vertical gradient into a pale
blue, black text — the only theme whose terminal palette is fully hardcoded
to the real 16-color VGA/console palette rather than derived from its own
`SYN_*` UI colors. Previously its own "Homage" category; now classified
structurally under Bevel — its gradient-titlebar, beveled look matches the
family, and its genuinely light `SYN_BG` (`#c0c0c0`) places it in Bevel's
light set rather than dark. It keeps its own exact-name override templates
for LabWC, Waybar, and `foot` regardless (see
[Theme Engine](./theme-engine.md#theme-specific-override-templates-matrix-and-win95)).

![SYN-OS-WIN95 desktop](../screenshots/desktop-SYN-OS-WIN95.png)

*Desktop with the SYN-OS-WIN95 wallpaper, Waybar in the silver/blue palette,
a window showing the gradient titlebar, and an open terminal in the
classic console color palette.*

#### SYN-OS-BEVEL-STEEL

Glyph: `⬢`

Near-black with a blue-gray tint (`#0d0f12`), muted steel-blue accent
(`#7fb8dd`), pale blue-gray text (`#e8edf2`).

![SYN-OS-BEVEL-STEEL desktop](../screenshots/desktop-SYN-OS-BEVEL-STEEL.png)

*Desktop with the SYN-OS-BEVEL-STEEL wallpaper, a window with a vertical
gradient titlebar, Waybar's gradient module backgrounds, and an open
terminal.*

#### SYN-OS-BEVEL-COPPER

Glyph: `⬢`

Near-black with warm undertones (`#0d0805`), warm copper-orange accent
(`#e6935e`), pale peach text (`#ffe8d9`).

![SYN-OS-BEVEL-COPPER desktop](../screenshots/desktop-SYN-OS-BEVEL-COPPER.png)

*Desktop with the SYN-OS-BEVEL-COPPER wallpaper, gradient titlebar and
button chrome, and an open terminal in the copper-on-black palette.*

#### SYN-OS-BEVEL-EMERALD

Glyph: `⬢`

Near-black with a green tint (`#050d08`), bright emerald accent (`#4dd991`),
pale mint text (`#d9ffe8`).

![SYN-OS-BEVEL-EMERALD desktop](../screenshots/desktop-SYN-OS-BEVEL-EMERALD.png)

*Desktop with the SYN-OS-BEVEL-EMERALD wallpaper, the bar's drop shadow
visible along its edge, and an open terminal in the emerald-on-black
palette.*

#### SYN-OS-BEVEL-SLATE

Glyph: `⬢`

Near-black with a cool gray tint (`#0d0d0f`), soft periwinkle accent
(`#a3b8e6`), pale gray-blue text (`#e8ecf5`).

![SYN-OS-BEVEL-SLATE desktop](../screenshots/desktop-SYN-OS-BEVEL-SLATE.png)

*Desktop with the SYN-OS-BEVEL-SLATE wallpaper, a gradient titlebar, and an
open terminal in the slate-on-black palette.*

#### SYN-OS-BEVEL-WINE

Glyph: `⬢`

Near-black with a red tint (`#0d0508`), bright wine-pink accent (`#e6668a`),
pale pink text (`#ffe0e8`).

![SYN-OS-BEVEL-WINE desktop](../screenshots/desktop-SYN-OS-BEVEL-WINE.png)

*Desktop with the SYN-OS-BEVEL-WINE wallpaper, gradient titlebar and button
chrome, and an open terminal in the wine-on-black palette.*

#### SYN-OS-BEVEL-BRONZE

Glyph: `⬢`

Near-black with warm undertones (`#0a0805`), muted bronze accent
(`#dcb066`), pale cream text (`#fff0d9`).

![SYN-OS-BEVEL-BRONZE desktop](../screenshots/desktop-SYN-OS-BEVEL-BRONZE.png)

*Desktop with the SYN-OS-BEVEL-BRONZE wallpaper, a vertical-gradient
titlebar, and an open terminal in the bronze-on-black palette.*

#### SYN-OS-BEVEL-TEAL

Glyph: `⬢`

Near-black with a teal tint (`#050d0d`), bright teal accent (`#5cd9d9`),
pale mint text (`#d9fffa`).

![SYN-OS-BEVEL-TEAL desktop](../screenshots/desktop-SYN-OS-BEVEL-TEAL.png)

*Desktop with the SYN-OS-BEVEL-TEAL wallpaper, Waybar's gradient module
backgrounds, and an open terminal in the teal-on-black palette.*

#### SYN-OS-BEVEL-PLUM

Glyph: `⬢`

Near-black with a violet tint (`#0a050d`), bright plum-violet accent
(`#b87ae6`), pale lavender text (`#f0e0ff`).

![SYN-OS-BEVEL-PLUM desktop](../screenshots/desktop-SYN-OS-BEVEL-PLUM.png)

*Desktop with the SYN-OS-BEVEL-PLUM wallpaper, gradient titlebar and button
chrome, and an open terminal in the plum-on-black palette.*

## Light

### Vanilla

Original SYN-OS palettes with no external reference point: flat color
fields, no borders beyond a hairline, no gradients or shadows — the
light-background counterparts to Vanilla-dark.

#### SYN-OS-BRIGHT

Glyph: *(none)*

The one genuinely light-background theme among the original 14, in the
"everyday" sense — near-white background (`#f5f5f5`), dark text
(`#1a1a1a`), restrained blue accent (`#0a2e5c`). Previously grouped under
"Neutral"; reclassified as Vanilla-light since it's structurally plain flat
color fields with no distinguishing border or shadow treatment. Its
background and text colors are unchanged, though its accent was updated
(from a lighter blue) as part of a separate contrast fix — the old accent
failed a 4.5 contrast ratio against this light background.

![SYN-OS-BRIGHT desktop](../screenshots/desktop-SYN-OS-BRIGHT.png)

*Desktop with the SYN-OS-BRIGHT wallpaper, Waybar in light mode, and an open
terminal showing dark-on-light text.*

#### SYN-OS-SILVER

Glyph: *(none)*

Light gray-on-gray (`#c8c8c8` background), near-black accent (`#1a1c20`),
near-black text (`#0d0d0d`) — the other light-background theme besides
BRIGHT, cooler and grayer rather than near-white. Previously grouped under
"Neutral"; reclassified as Vanilla-light for the same structural reason as
BRIGHT. Its background and text colors are unchanged, though its accent was
likewise updated as part of the same contrast fix — the old accent (a mid
slate gray) failed a 4.5 contrast ratio against this light background.

![SYN-OS-SILVER desktop](../screenshots/desktop-SYN-OS-SILVER.png)

*Desktop with the SYN-OS-SILVER wallpaper, Waybar in the gray palette, and
an open terminal, for comparison against SYN-OS-BRIGHT.*

#### SYN-OS-VANILLA-LIGHT-CREAM

Glyph: `●`

Warm off-white background (`#faf6ee`), dark brown accent (`#3d2900`), dark
warm-gray text (`#1a1712`).

![SYN-OS-VANILLA-LIGHT-CREAM desktop](../screenshots/desktop-SYN-OS-VANILLA-LIGHT-CREAM.png)

*Desktop with the SYN-OS-VANILLA-LIGHT-CREAM wallpaper, Waybar in light
mode, and an open terminal showing dark text on the warm cream background.*

#### SYN-OS-VANILLA-LIGHT-FROST

Glyph: `❄`

Cool off-white background (`#f0f4f7`), dark blue accent (`#003350`), dark
blue-gray text (`#12161a`).

![SYN-OS-VANILLA-LIGHT-FROST desktop](../screenshots/desktop-SYN-OS-VANILLA-LIGHT-FROST.png)

*Desktop with the SYN-OS-VANILLA-LIGHT-FROST wallpaper, Waybar in light
mode, and an open terminal showing dark text on the cool frost background.*

### Flatline

Zero border-radius, zero shadow, hairline borders only. Waybar modules have
no panel background at all — just text on the bar's own background. The
light-background counterparts to Flatline-dark.

#### SYN-OS-FLATLINE-LIGHT-INK

Glyph: `▪`

Neutral near-white background (`#f5f5f5`), near-black accent and text
(`#1a1a1a` both) — no hue at all, the Flatline family's monochrome light
entry.

![SYN-OS-FLATLINE-LIGHT-INK desktop](../screenshots/desktop-SYN-OS-FLATLINE-LIGHT-INK.png)

*Desktop with the SYN-OS-FLATLINE-LIGHT-INK wallpaper, Waybar's bare-text
modules, and an open terminal in the grayscale light palette.*

#### SYN-OS-FLATLINE-LIGHT-ROSE

Glyph: `▪`

Pale pink-white background (`#f7f2f4`), dark rose accent (`#5c1a33`),
near-black text (`#1a1a1a`).

![SYN-OS-FLATLINE-LIGHT-ROSE desktop](../screenshots/desktop-SYN-OS-FLATLINE-LIGHT-ROSE.png)

*Desktop with the SYN-OS-FLATLINE-LIGHT-ROSE wallpaper, hairline-bordered
windows, and an open terminal in the rose-on-white palette.*

#### SYN-OS-FLATLINE-LIGHT-AMBER

Glyph: `▪`

Warm off-white background (`#f7f5ee`), dark amber-brown accent
(`#3d2900`), dark warm-gray text (`#1a1712`).

![SYN-OS-FLATLINE-LIGHT-AMBER desktop](../screenshots/desktop-SYN-OS-FLATLINE-LIGHT-AMBER.png)

*Desktop with the SYN-OS-FLATLINE-LIGHT-AMBER wallpaper, Waybar's bare-text
modules, and an open terminal in the amber-on-cream palette.*

#### SYN-OS-FLATLINE-LIGHT-SKY

Glyph: `▪`

Pale blue-white background (`#eff5f7`), dark teal-blue accent (`#003040`),
dark blue-gray text (`#141a1c`).

![SYN-OS-FLATLINE-LIGHT-SKY desktop](../screenshots/desktop-SYN-OS-FLATLINE-LIGHT-SKY.png)

*Desktop with the SYN-OS-FLATLINE-LIGHT-SKY wallpaper, hairline-bordered
windows, and an open terminal in the sky-blue-on-white palette.*

### Slab

Thick (6px) square-cornered borders, chunky bordered/margined Waybar module
blocks. The light-background counterparts to Slab-dark.

#### SYN-OS-SLAB-LIGHT-STONE

Glyph: `■`

Warm off-white background (`#f0efe8`), dark brown accent (`#332200`), dark
warm-gray text (`#1a1712`).

![SYN-OS-SLAB-LIGHT-STONE desktop](../screenshots/desktop-SYN-OS-SLAB-LIGHT-STONE.png)

*Desktop with the SYN-OS-SLAB-LIGHT-STONE wallpaper, Waybar's chunky
bordered module blocks, and an open terminal in the stone-on-cream palette.*

#### SYN-OS-SLAB-LIGHT-SAGE

Glyph: `■`

Pale green-white background (`#eef2ec`), dark forest-green accent
(`#122611`), dark green-gray text (`#141a12`).

![SYN-OS-SLAB-LIGHT-SAGE desktop](../screenshots/desktop-SYN-OS-SLAB-LIGHT-SAGE.png)

*Desktop with the SYN-OS-SLAB-LIGHT-SAGE wallpaper, thick square-cornered
borders, and an open terminal in the sage-on-white palette.*

#### SYN-OS-SLAB-LIGHT-DUSK

Glyph: `■`

Pale violet-white background (`#f2eef2`), dark plum accent (`#3d1a3d`),
dark violet-gray text (`#1a141a`).

![SYN-OS-SLAB-LIGHT-DUSK desktop](../screenshots/desktop-SYN-OS-SLAB-LIGHT-DUSK.png)

*Desktop with the SYN-OS-SLAB-LIGHT-DUSK wallpaper, Waybar's margined module
blocks, and an open terminal in the dusk-on-white palette.*

#### SYN-OS-SLAB-LIGHT-CLAY

Glyph: `■`

Warm off-white background (`#f2ede8`), dark clay-brown accent (`#361c0c`),
dark warm-gray text (`#1a1410`).

![SYN-OS-SLAB-LIGHT-CLAY desktop](../screenshots/desktop-SYN-OS-SLAB-LIGHT-CLAY.png)

*Desktop with the SYN-OS-SLAB-LIGHT-CLAY wallpaper, thick square-cornered
borders, and an open terminal in the clay-on-cream palette.*

### Halo

Glow/outline styling: accent-colored borders with no fill (backgrounds match
`SYN_BG` exactly), plus a box-shadow glow on the active Waybar taskbar
button. The light-background counterparts to Halo-dark.

#### SYN-OS-HALO-LIGHT-AZURE

Glyph: `◈`

Pure white background (`#ffffff`), dark azure-blue accent (`#052d47`),
near-black text (`#1a1a1a`).

![SYN-OS-HALO-LIGHT-AZURE desktop](../screenshots/desktop-SYN-OS-HALO-LIGHT-AZURE.png)

*Desktop with the SYN-OS-HALO-LIGHT-AZURE wallpaper, azure glow-outlined
window borders, and an open terminal in the azure-on-white palette.*

#### SYN-OS-HALO-LIGHT-CORAL

Glyph: `◈`

Pale warm-white background (`#fff7f5`), dark coral-red accent (`#5c1a0d`),
dark warm text (`#1a1210`).

![SYN-OS-HALO-LIGHT-CORAL desktop](../screenshots/desktop-SYN-OS-HALO-LIGHT-CORAL.png)

*Desktop with the SYN-OS-HALO-LIGHT-CORAL wallpaper, coral glow-outlined
borders, and an open terminal in the coral-on-white palette.*

#### SYN-OS-HALO-LIGHT-MINT

Glyph: `◈`

Pale green-white background (`#f5fff9`), dark green accent (`#053018`),
dark green-gray text (`#101a14`).

![SYN-OS-HALO-LIGHT-MINT desktop](../screenshots/desktop-SYN-OS-HALO-LIGHT-MINT.png)

*Desktop with the SYN-OS-HALO-LIGHT-MINT wallpaper, mint glow-outlined
window borders, and an open terminal in the mint-on-white palette.*

#### SYN-OS-HALO-LIGHT-ORCHID

Glyph: `◈`

Pale violet-white background (`#fdf5ff`), dark orchid-purple accent
(`#3d0d5c`), dark violet text (`#181021`).

![SYN-OS-HALO-LIGHT-ORCHID desktop](../screenshots/desktop-SYN-OS-HALO-LIGHT-ORCHID.png)

*Desktop with the SYN-OS-HALO-LIGHT-ORCHID wallpaper, orchid glow-outlined
borders, and an open terminal in the orchid-on-white palette.*

### Bevel

`Gradient Vertical` titlebars and buttons, `linear-gradient()` Waybar module
backgrounds, a drop shadow on the bar itself. The light-background
counterparts to Bevel-dark, alongside WIN95.

#### SYN-OS-BEVEL-LIGHT-PEARL

Glyph: `⬢`

Neutral off-white background (`#f2f2f0`), dark olive-gray accent
(`#26261f`), near-black text (`#1a1a18`).

![SYN-OS-BEVEL-LIGHT-PEARL desktop](../screenshots/desktop-SYN-OS-BEVEL-LIGHT-PEARL.png)

*Desktop with the SYN-OS-BEVEL-LIGHT-PEARL wallpaper, a gradient titlebar,
and an open terminal in the pearl-on-white palette.*

#### SYN-OS-BEVEL-LIGHT-BLUSH

Glyph: `⬢`

Pale pink-white background (`#f7f0f2`), dark wine accent (`#331923`),
near-black text (`#1a1416`).

![SYN-OS-BEVEL-LIGHT-BLUSH desktop](../screenshots/desktop-SYN-OS-BEVEL-LIGHT-BLUSH.png)

*Desktop with the SYN-OS-BEVEL-LIGHT-BLUSH wallpaper, gradient titlebar and
button chrome, and an open terminal in the blush-on-white palette.*

#### SYN-OS-BEVEL-LIGHT-SEAFOAM

Glyph: `⬢`

Pale green-white background (`#eff5f2`), dark forest-green accent
(`#0c2917`), dark green-gray text (`#121a16`).

![SYN-OS-BEVEL-LIGHT-SEAFOAM desktop](../screenshots/desktop-SYN-OS-BEVEL-LIGHT-SEAFOAM.png)

*Desktop with the SYN-OS-BEVEL-LIGHT-SEAFOAM wallpaper, the bar's drop
shadow visible along its edge, and an open terminal in the
seafoam-on-white palette.*

## Related docs

- [Theme Engine](./theme-engine.md) — the full mechanism: `.theme` file
  format, `SYN_*` variable reference, Mode/Family structure, template
  rendering, and the live apply flow.
- [LabWC](../labwc.md) — where the Themes menu itself lives in the desktop.
