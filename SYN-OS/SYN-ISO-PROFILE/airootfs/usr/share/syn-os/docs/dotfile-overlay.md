# Dotfile Overlay

If you want to change a config file, a theme, or anything under `.config`, edit it in `DotfileOverlay/` (`SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/DotfileOverlay/`). That's where every per-user default lives in this repo — the one exception is `/usr/share/syn-os/docs`, which is static system data deployed by its own step in `syn-pacstrap.zsh`, not part of this overlay (see [Stage 0](./stage0.md#5-pacstrapmain-syn-pacstrapzsh)).

It's just a plain folder that's laid out exactly like the real filesystem it gets copied onto:

```
DotfileOverlay/
├── etc/
│   └── skel/                    → /etc/skel/       (new-user home template)
│       ├── .zshrc
│       ├── .wallpaper/          SYN-OS-{RED,BLUE,GREEN,PURPLE}-wallpaper.png
│       └── .config/
│           ├── labwc/           see docs/labwc.md (config only: rc.xml,
│           │                     menu.xml, autostart, environment, themerc)
│           ├── waybar/          see docs/waybar.md (config only: config.jsonc,
│           │                     style.css)
│           ├── syn-os/themes/   SYN-OS-{RED,BLUE,GREEN,PURPLE}.theme (SYN_* vars)
│           ├── foot/
│           ├── superfile/
│           ├── falkon/
│           ├── vlc/
│           ├── qt5ct/
│           └── pavucontrol-qt/
└── usr/
    ├── local/bin/                → /usr/local/bin/
    │                                (incl. syn-theme-apply, syn-docs-view.zsh)
    ├── lib/
    │   └── syn-os/
    │       ├── syn-pipe-*.zsh    labwc pipe-menu generators (audio, display,
    │       │                      docs, share, superfile, theme)
    │       ├── syn-bar-*.zsh     waybar modules/handlers (disk, launcher,
    │       │                      power, share-quickmenu, share-status,
    │       │                      toggle-position, wifi)
    │       └── theme-templates/  → /usr/lib/syn-os/theme-templates/
    │                                (waybar/labwc/qt5ct/foot/superfile
    │                                 templates rendered by syn-theme-apply)
    └── share/
        └── themes/
            └── SYN-OS-RED/       → /usr/share/themes/SYN-OS-RED/
                └── openbox-3/    (LabWC reads Openbox-format theme dirs;
                                    only RED needs one here — the other
                                    three themes work entirely through the
                                    SYN_* variables + templates above)
```

## How it actually gets onto a real system

![DotfileOverlay: from repo to a real user's home directory, across the Stage 0/Stage 1 boundary](./diagrams/svg/dotfile-overlay.svg)

Two steps, in two different stages:

1. **Stage 0**, still in the live ISO, `pacstrapMain` in [`syn-pacstrap.zsh`](./stage0.md#5-pacstrapmain-syn-pacstrapzsh) runs one command: `cp -r DotfileOverlay/* "${RootMountLocation}/"`. The folder is already laid out like the real filesystem, so this single copy puts everything in the right place — no per-file logic needed. Right after, it runs `chmod -R +x` on a few specific folders (`/usr/lib/syn-os` — where the `syn-pipe-*`/`syn-bar-*` scripts actually live — plus `labwc`'s `autostart` and the `superfile` config folder under `/etc/skel`, since those still need to execute in place), because a plain file copy doesn't always keep the execute bit on scripts.

2. **Stage 1**, inside the chroot, creates the actual user account: `useradd -m -G wheel`. The `-m` flag copies `/etc/skel` into the new user's home directory. This is the real moment your dotfiles become someone's actual files, not the `cp -r` in Stage 0.

Because this happens through `/etc/skel`, not `/home/<user>` directly, any user created afterward — including one you add by hand after install — gets the same defaults for free. The overlay only ever has to target `/etc/skel` once.

## Your dev machine is not the source of truth

If you're editing this repo from a computer that's already running SYN-OS, your live `~/.config` is a snapshot from whenever that machine's system was installed or last rebuilt. It doesn't update itself when you edit this repo, or even after you commit here.

Editing your live `~/.config/waybar/config.jsonc` changes your desktop right now, but does nothing to `DotfileOverlay/`, and won't show up in the next ISO unless you copy the same edit into the repo by hand. Editing `DotfileOverlay/` here does nothing to your current desktop — it only changes what future installs or rebuilds get. The two only ever meet at install or rebuild time, never while you're using the system. Don't assume they match just because they look similar; if you need to know whether they've drifted, diff them explicitly.
