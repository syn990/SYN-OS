# The theme system

SYN-OS ships with 63 themes, and switching between them changes
everything at once: the bar, the window borders, the terminal, every
app's colors, the wallpaper. Pick one from Preferences → Themes and the
whole desktop follows, no restart needed for most of it.

See the [Theme gallery](./theme-gallery.md) for a look at all of them.

## Picking one

Themes are organized two ways: dark or light, and by style family. There
are five families:

- **Vanilla**, the original clean, flat look SYN-OS started with.
- **Flatline**, the most minimal, no borders or shadows at all.
- **Slab**, thick borders and chunky blocks.
- **Halo**, a glowing outline look.
- **Bevel**, gradients and depth, closer to an older, skeuomorphic style.

Every family comes in both dark and light versions, so switching mode
never means switching style, only the color palette underneath it.

Two themes stand a bit apart: **MATRIX** (a green-on-black terminal look)
and **WIN95** (a nod to classic Windows 95). Both get their own hand-tuned
extra detail beyond what their family normally provides.

## What updates live, and what needs a new window

Most of it changes the instant you pick a theme: the bar, notifications,
window borders and menus, and your wallpaper.

A couple of things only update the next time you open them:

- **Terminal windows** already open keep their old colors. New ones you
  open pick up the new theme.
- **Qt and GTK apps** (the file manager, the browser) pick up the new
  theme the next time you launch them.

## Making your own

Themes are just plain text files, easy to read and copy. If you want to
make your own, copy an existing one under
`~/.config/syn-os/themes/` and change the colors. Nothing fancy is
needed, just a text editor.
