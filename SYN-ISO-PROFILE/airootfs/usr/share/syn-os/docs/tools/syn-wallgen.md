# Wallpapers (syn-wallgen)

Every theme gets its own 1920x1080 wallpaper, and none of them come from
a stock photo folder. They're generated on the spot.

## How it looks

Each wallpaper starts as a radial gradient in the theme's own colors,
then gets a pattern on top that matches its style family: a hairline
grid for Flatline, bands for Slab, glowing rings for Halo, a soft sheen
for Bevel. Vanilla stays plain, just the gradient, a glow, and a
vignette around the edges.

The light position and pattern density get re-rolled every time it
runs, so generating the same theme's wallpaper twice gives you two
wallpapers that are close, but never identical.

## When it runs

Automatically, twice: once during install, against your new account,
and once whenever someone builds a fresh ISO, so the skel files it ships
with already have a wallpaper for every theme.

You can also run it yourself any time, most usefully after adding your
own theme:

```bash
syn-wallgen --themes-dir ~/.config/syn-os/themes --out-dir ~/.wallpaper
```

Both flags default to those same paths, so plain `syn-wallgen` with no
arguments works for the normal case. Add `--only-missing` to skip themes
that already have a wallpaper, or `--suffix` to tag the output filenames.

See [The theme system](../theming/theme-engine.md) for how a theme file
is put together, and [Theme gallery](../theming/theme-gallery.md) for
every theme this can generate a wallpaper for.
