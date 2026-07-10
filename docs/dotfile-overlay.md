# Dotfile Overlay

`DotfileOverlay/` (`SYN-ISO-PROFILE/airootfs/usr/lib/syn-os/DotfileOverlay/`) is the *only* place dotfile and desktop-config edits should be made in this repo. It's a plain filesystem tree, structured to mirror where its contents land on the installed system:

```
DotfileOverlay/
├── etc/
│   └── skel/                    → /etc/skel/       (new-user home template)
│       ├── .zshrc
│       ├── .wallpaper/
│       └── .config/
│           ├── labwc/           see docs/labwc.md
│           ├── waybar/          see docs/waybar.md
│           ├── foot/
│           ├── superfile/
│           ├── falkon/
│           ├── vlc/
│           ├── qt5ct/
│           └── pavucontrol-qt/
└── usr/
    ├── local/bin/                → /usr/local/bin/
    ├── lib/                      → /usr/lib/
    └── share/
        └── themes/
            └── SYN-OS-RED/       → /usr/share/themes/SYN-OS-RED/
                └── openbox-3/    (LabWC reads Openbox-format theme dirs)
```

## How it's deployed

`syn-pacstrap.zsh`'s `pacstrapMain` (see [Stage 0](./stage0.md#5-pacstrapmain-syn-pacstrapzsh)) copies the entire tree onto the target system near the end of the base install:

```zsh
cp -r /usr/lib/syn-os/DotfileOverlay/* "${RootMountLocation}/"
```

Because the overlay's internal structure already mirrors absolute paths (`etc/skel/...`, `usr/local/bin/...`), this single `cp -r` is enough to place everything correctly, so there's no per-file mapping logic to maintain. Immediately after, the install script `chmod -R +x`'s a few specific directories (`/usr/lib/syn-os`, `/etc/skel/.config/labwc`, `/etc/skel/.config/waybar`, `/etc/skel/.config/superfile`) so scripts inside them are executable on the new system, since file permissions from the ISO build environment aren't guaranteed to survive the copy with execute bits intact.

## Why `/etc/skel` and not `/home/<user>` directly

Stage 1 creates the user account (`useradd -m`) *after* Stage 0's pacstrap step has already deployed the overlay into `/etc/skel`. `useradd -m` populates the new home directory from `/etc/skel` at account-creation time, so the overlay only needs to target `/etc/skel` once, and every user created afterward (including ones you add manually post-install) inherits the same defaults.

## Critical caveat: this is not what's on your running dev machine

If you're working on this repo from an already-installed SYN-OS system, your live `/etc/skel` (and your own `~/.config/...`) are a **snapshot from whenever that system's ISO was built or last reinstalled**. They do not auto-sync with edits to this repo, or even with commits already merged into it. Editing your live `~/.config/waybar/config.jsonc` changes only your running session; it has no effect on `DotfileOverlay/`, and won't be reflected in the next ISO build unless you also make the same edit here.

Conversely, editing `DotfileOverlay/` has zero effect on your current desktop session: it only changes what gets deployed to *newly installed or rebuilt* systems. Don't diff your live config against the repo and assume parity; check modification dates or diff explicitly if you need to know whether they've drifted.
