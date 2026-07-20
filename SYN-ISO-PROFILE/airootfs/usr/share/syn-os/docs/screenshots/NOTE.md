# Screenshot checklist

This directory holds real screenshots referenced by the docs as markdown
image links. None exist yet — every reference below currently renders as a
broken image in `glow` (the docs viewer) until the actual PNG is captured
and dropped in at the exact filename listed. This file itself is not
markdown the docs pipe-menu picks up as a doc entry — it's just a checklist
for whoever captures these.

Capture at a reasonable desktop resolution (1920x1080 or similar), PNG,
no cursor/window-manager chrome cropping needed unless noted.

## Theme gallery (14) — referenced from `../theming/theme-gallery.md`

Each is the full desktop with that theme active: wallpaper, waybar visible,
one `foot` terminal open so the theme's terminal palette shows too.

- [ ] `desktop-SYN-OS-RED.png`
- [ ] `desktop-SYN-OS-BLUE.png`
- [ ] `desktop-SYN-OS-GREEN.png`
- [ ] `desktop-SYN-OS-M141.png`
- [ ] `desktop-SYN-OS-ORANGE.png`
- [ ] `desktop-SYN-OS-PINK.png`
- [ ] `desktop-SYN-OS-PURPLE.png`
- [ ] `desktop-SYN-OS-YELLOW.png`
- [ ] `desktop-SYN-OS-MATRIX.png`
- [ ] `desktop-SYN-OS-WIN95.png`
- [ ] `desktop-SYN-OS-BRIGHT.png`
- [ ] `desktop-SYN-OS-GRAPHITE.png`
- [ ] `desktop-SYN-OS-LIGHT.png`
- [ ] `desktop-SYN-OS-SILVER.png`

## UI spots (6)

- [ ] `menu-xml-root-open.png` — `Super+Space`, the root menu open over the
      desktop (referenced from `../labwc.md`)
- [ ] `menu-themes-pipe-open.png` — Preferences > Themes, showing the
      Vanilla/Homage/Neutral grouped submenus (referenced from
      `../theming/theme-engine.md`)
- [ ] `menu-docs-pipe-open.png` — Preferences > Docs, showing the flat
      top-level entries followed by the Theming/Tools/ISO Build/Concepts
      separators (referenced from `../labwc.md`)
- [ ] `syn-filemanager-main-window.png` — path bar, QTreeView listing, and
      toolbar (referenced from `../tools/syn-filemanager.md`)
- [ ] `waybar-closeup.png` — the full bar width at default height
      (referenced from `../waybar.md`)
- [ ] `rofi-power-menu.png` — Lock / Log Out / Reboot / Power Off, themed,
      positioned near the power icon (referenced from `../waybar.md`)
- [ ] `synshare-quickmenu.png` — the SYN-SHARE waybar quick-menu popup
      (referenced from `../tools/syn-share.md`)

20 files total. Grep for the exact list any time: `grep -rhoE
'screenshots/[a-zA-Z0-9_-]+\.png' .. --include='*.md' | sort -u`.
