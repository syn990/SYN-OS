# Glossary

Every script, package, and path in SYN-OS, present tense, sourced
straight from the code. The readme sells the project and the tool docs
explain how to use one thing at a time; this page is neither, it's the
map. If you want to know what file does something or where a setting
actually lives, it's here.

---

## Packages

Every package SYN-OS installs is declared in one file,
`/usr/lib/syn-os/syn-packages.zsh`, as seven arrays. The categories are
an organizational grouping only, not a functional boundary, nothing
enforces that a package "belongs" to its category at runtime.

**baseCore**: `base`, `linux`, `linux-firmware`, `archlinux-keyring`,
`reflector`, `opendoas`, `qemu-guest-agent`, `sof-firmware`,
`sof-tools`, `fuse`, `dosfstools`, `e2fsprogs`, `f2fs-tools`,
`btrfs-progs`, `xfsprogs`, `lvm2`, `cryptsetup`, `zram-generator`.
The kernel, firmware, filesystem and block-device tools for every
`FilesystemStrat`, and `opendoas` itself.

**netAndServices**: `dhcpcd`, `iwd`, `openvpn`, `dnsmasq`, `hostapd`,
`openssh`, `sshfs`, `waypipe`, `ffmpeg`, `sdl2`, `bluez`, `bluez-utils`.
`ffmpeg` and `sdl2` are here, not in `devToolkit`, specifically because
syn-relay's screen watch/host roles link against them at runtime on
every install profile, including the minimal one.

**shellAndCLI**: `zsh`, `zsh-completions`, `zsh-syntax-highlighting`,
`zsh-autosuggestions`, `fzf`, `zoxide`, `ripgrep`, `fd`, `bat`,
`inetutils`, `calc`, `git`, `btop`, `nano`, `foot`, `brightnessctl`,
`pamixer`, `pipewire-pulse`, `glow`, `parted`.

**desktopStack**: `labwc`, `wmenu`, `wlr-randr`, `grim`, `slurp`,
`archlinux-xdg-menu`, `waybar`, `mako`, `libnotify`, `swaybg`,
`swaylock`, `fuzzel`, `rofi`, `feh`, `qt5ct`, `qt6ct`, `kvantum`,
`kvantum-qt5`, `qt6-base`, `lxqt-archiver`, `featherpad`.

**devToolkit**: `base-devel`, `gcc`, `fakeroot`, `android-tools`,
`archiso`, `binwalk`, `hexedit`, `lshw`, `yt-dlp`,
`wayland-protocols`. `base-devel`/`gcc`/`fakeroot` exist here for
`makepkg`/AUR building on an installed system; the install pipeline
itself doesn't need them, every SYN-SOFTWARE tool ships prebuilt.

**fontsI18n**: `terminus-font`, `ttf-bitstream-vera`, `ttf-dejavu`,
`noto-fonts`, `noto-fonts-emoji`, `noto-fonts-cjk`, `ttf-liberation`,
`ttf-terminus-nerd`, `otf-font-awesome`.

**appsMedia**: `vlc`, `openra`, `audacity`, `obs-studio`, `falkon`,
`gimp`.

Two combined arrays actually get installed:

- **SYNSTALL**: all seven categories, used when `PackageProfile=full`.
- **SYNMINIMAL**: baseCore + netAndServices + shellAndCLI +
  desktopStack, plus three font packages (`terminus-font`,
  `ttf-dejavu`, `ttf-terminus-nerd`), used when
  `PackageProfile=minimal`. No devToolkit, no appsMedia, no CJK fonts.

Neither array installs a single SYN-SOFTWARE native tool by name.
Those get copied onto the disk separately, already built, by
`syn-pacstrap.zsh` (below), not pulled from a package repository.

---

## Configuring an install: two different tools, same name

This is the part most likely to confuse anyone reading the scripts
cold, so it gets its own section. `synos-config` and
`/usr/lib/syn-os/syn-config.zsh` sound like the same thing. They are
not. Neither sources the other.

### `synos-config` runs `syn-conf-picker.zsh`

The `synos-config` alias in the live-ISO zshrc runs `doas zsh
/usr/lib/syn-os/syn-conf-picker.zsh`. This is the interactive editor:
it greps every `KEY="value"` line out of `/etc/syn-os/synos.conf`,
lists them in `fzf` with a live preview pane (the preview is the
script re-invoking itself with `--preview-line N` to print the comment
block sitting above that key), lets you type a replacement value, and
`sed`s it back into the file in place. It rejects a value containing a
double quote (that would break the file's own `KEY="value"` shape) but
does nothing else in the way of validation, no type checking, no
knowledge of which values are legal for which key. It exists because
the live ISO boots to a plain tty1 shell with no compositor running,
so nothing rofi-based works yet. `nano /etc/syn-os/synos.conf` is
still there for anything the picker doesn't cover, like adding a key
that isn't in the file yet.

### `syn-config.zsh` is the loader every install actually runs on

This one is never run by hand. `syn-stage0.zsh` and `syn-stage1.zsh`
both source it, and it's where the real logic lives. It sources
`synos.conf` itself, then:

- normalizes boolean-ish values (`y`/`yes`/`true`/`1` and their
  opposites) to a plain `yes`/`no` for `RequireWipeConfirm`,
  `Encryption`, `UseLvm`, `EnableSsh`;
- detects real firmware unless `BootMode` overrides it, checking for
  `/sys/firmware/efi/efivars` to set `SynosEnv` to `UEFI` or `MBR`;
- resolves `PartitionStrat=auto` against that detected firmware and
  `Encryption`, picking `uefi-bootctl`, `mbr-grub`, or `mbr-syslinux`;
- derives `VolumeStrat` from `Encryption` and `UseLvm` (see table
  below);
- runs a set of hard `:?`-style checks, `Hostname`, `UserAccountName`,
  `Locale`, disk-is-a-block-device, `UserAccountPassword` isn't still
  `CHANGE_ME`, and more, every one of which aborts the install before
  Stage 0 touches the disk;
- catches an explicit `PartitionStrat` that contradicts the machine's
  real firmware (a `synos.conf` copied from a UEFI box onto BIOS
  hardware, for instance) and refuses to continue;
- exports the whole normalized set for Stage 0 and Stage 1 to use.

### What `PartitionStrat` actually accepts

| Value | Firmware | Notes |
|---|---|---|
| `uefi-bootctl` | UEFI | The default UEFI path, systemd-boot |
| `uefi-refind` | UEFI | rEFInd, chains to the systemd-boot entry |
| `uefi-clover` | UEFI | Accepted here as reserved for future support; errors clearly once Stage 1 actually tries to install a bootloader |
| `mbr-syslinux` | BIOS | No encryption support, syslinux can't read LUKS |
| `mbr-grub` | BIOS | GRUB, supports encryption |
| `mbr-grub-btrfs` | BIOS | GRUB with a btrfs `/boot` |
| `mbr-grub-xfs` | BIOS | GRUB with an xfs `/boot` |

`Encryption` and `UseLvm` are independent yes/no flags that combine
into `VolumeStrat`: both yes gives `luks-lvm`, encryption only gives
`luks-only`, LVM only gives `lvm-only`, neither gives `plain`.
`FilesystemStrat` accepts `ext4`, `f2fs`, `btrfs`, or `xfs`.
`PackageProfile` accepts `full` or `minimal`, see Packages above.

---

## The installer pipeline

In the order a fresh install actually runs them.

**`syn-config.zsh`** loads and validates `synos.conf`, covered above.
Sourced by both stages, never run directly.

**`syn-ui.zsh`** is the shared output library for the whole install:
a dark red/black color palette plus per-stage accent colors,
`syn_ui::step`/`step_done`/`step_fail`/`info`/`error` for consistent
status lines, `syn_ui::confirm_wipe` for the one required y/N gate
before anything destructive happens, and the ASCII banners shown
during install. `syn_ui::doas` (routes through
`syn-uplink-dialpad --exec`) is defined here too but is desktop-only,
nothing in Stage 0 or Stage 1 actually calls it, since neither runs
inside a graphical session.

**`syn-disk.zsh`** defines the four-step disk pipeline:
`partitionMain` → `volumeMain` → `filesystemMain` → `mountMain`, each
one dispatching to a named function for whatever strategy `synos.conf`
resolved to, `partitionStrat_uefi_bootctl`, `volumeStrat_luks_lvm`,
and so on. It also tears down stale LUKS/LVM/device-mapper state left
behind by a prior interrupted install before repartitioning. It relies
on `syn-ui.zsh` already being sourced, by design, and is only ever
called that way from `syn-stage0.zsh`.

**`syn-packages.zsh`** defines the package arrays, covered above.

**`syn-pacstrap.zsh`** defines `pacstrapMain`: runs `reflector`,
initializes the pacman keyring, picks `SYNSTALL` or `SYNMINIMAL` plus
whatever bootloader packages the resolved `PartitionStrat` needs, then
`pacstrap -K` and `genfstab`. It also copies `synos.conf` (password
stripped) and every installer script onto the target disk, deploys
`DotfileOverlay/` if one is present, copies each prebuilt native tool
across from the live ISO (nothing compiles here, everything was built
at ISO-build time), and writes `/etc/syn-os/install.state` with the
disk facts (`RootFsDev`, `SwapDev`, `LuksUuid`, and so on) that Stage 1
needs once it's running inside a different filesystem.

**`syn-stage0.zsh`** is what `synos-install` actually runs. It
sources the four scripts above, re-execs itself under `script -qefc`
so the whole install gets logged to a real file with a pty attached
(pacstrap's progress bars need a real tty), registers a cleanup trap
that force-unmounts everything on exit, runs `syn_ui::confirm_wipe`
unless `RequireWipeConfirm=no`, then runs the disk pipeline followed
by `pacstrapMain`, then hands off with `arch-chroot $RootMountLocation
/bin/zsh /usr/lib/syn-os/syn-stage1.zsh`.

**`syn-stage1.zsh`** runs inside that chroot. It reads
`install.state`, sets locale/hostname/timezone/console keymap, writes
the keyboard layout into the new user's own `labwc/environment`,
configures `doas` (plus a `sudo` shim), creates the user account and
strips the password out of `synos.conf` the moment it's used, runs
`syn-wallgen` to generate that user's wallpapers, builds the initramfs
with `mkinitcpio`, installs whichever bootloader `PartitionStrat`
resolved to, enables baseline services, and finishes with
`syn_ui::final_banner`.

---

## Encryption and file-transfer tools, and a real naming collision

**`syn-crypter.zsh`** (`/usr/lib/syn-os/syn-crypter.zsh`) is a
standalone CLI that shells out to `openssl` for AES-256/Blowfish/RSA,
and for its `--redshirt` mode compiles a small embedded C XOR helper
on first use. This is a completely different program from the native
`syn-crypter` binary covered in [Encryption](./tools/syn-crypter.md),
which links `libcrypto` directly and has its own ncurses TUI. The
desktop's own `.zshrc` aliases the bare name `syn-crypter` to this
zsh script, which means the alias shadows the native binary of the
same name. Typing the full path,
`/usr/lib/syn-os/syn-crypter`, gets you the native tool; typing
`syn-crypter` in a terminal on the installed desktop gets you this
script instead.

**`syn-redshirt.zsh`** is a separate, fuller XOR implementation again,
not shared code with `syn-crypter.zsh`'s `--redshirt` mode or with the
native binary's own Redshirt variants. It writes a versioned header
plus a SHA1 integrity check and auto-detects encrypt-vs-decrypt by
reading whatever header is already on the target file. The desktop's
`syn-redshirt-prompt.zsh` (a rofi front end for this script) currently
has no caller in `menu.xml`, its own comment there says SYN-CRYPTER's
algorithm menu covers Redshirt now, so this path exists in the
codebase but isn't reachable from the desktop menu at present.

**`syn-share.zsh`** and **`syn-share-lib.zsh`** split cleanly: the
`.zsh` file is a thin `case` dispatcher over subcommands like
`srv-start-rsync` or `cli-smb-mount`, and `syn-share-lib.zsh` (sourced
only, never run directly) holds every actual function behind those
subcommands, plus the shared paths (`/srv/syn-share/`,
`/etc/syn-share/rsync.secrets`). See [File sharing](./tools/syn-share.md)
for the six protocols themselves.

---

## The desktop scripts

Everything below lives under `DotfileOverlay/usr/lib/syn-os/`, which
means it ships to `/usr/lib/syn-os/` on the installed system, not the
live ISO. Grouped by what they're for.

### Capture

- **`screenshot.zsh`**: full or region (via `slurp`) screenshot with
  `grim`, saved to a directory you pick (default
  `~/Pictures/Screenshots`), named by timestamp.
- **`screen-recorder.zsh`**: same full/region split, backed by
  `wf-recorder`. Checks a pidfile in `$XDG_RUNTIME_DIR` first, so
  invoking it again while a recording is running stops that recording
  instead of starting a second one. Saves to `~/Videos/`.

### The bar

- **`syn-bar-launcher.zsh`**: the launcher button's click handler,
  themes and execs `wmenu-run`.
- **`syn-bar-power.zsh`**: the power button's click handler, a themed
  `rofi` menu for Lock/Log Out/Reboot/Power Off.
- **`syn-bar-toggle-position.zsh`**: flips waybar between top and
  bottom by rewriting `~/.config/waybar/config.jsonc` directly, then
  signals waybar to reload live. The only bar script that mutates
  config state rather than just reading it.
- **`syn-bar-share-status.zsh`**: polled every 5 seconds by waybar's
  SYN-SHARE module, checks all six file-sharing services and emits
  the module's JSON.
- **`syn-bar-share-quickmenu.zsh`**: the SYN-SHARE indicator's click
  handler, a smaller rofi popup mirroring the full pipe menu below.

### Shared libraries

Sourced only, never run on their own.

- **`syn-theme-lib.zsh`**: the theme-state library, written in plain
  POSIX sh (not zsh) because labwc's autostart runs under `/bin/sh`.
  `syn_theme_current()` reads the active theme's name from
  `~/.config/syn-os/current-theme`; `syn_theme_load()` dot-sources
  `~/.config/syn-os/themes/<name>.theme` straight into the caller, so
  its `SYN_*` color variables land in whatever script called it. It
  doesn't apply a theme itself, that's `syn-theme-apply`, see [What a
  theme switch actually does](#what-a-theme-switch-actually-does)
  below.
- **`syn-picker-lib.zsh`**: four themed picker functions used almost
  everywhere in this list, `syn_pick::wmenu`, `syn_pick::rofi`,
  `syn_pick::rofi_input`, `syn_pick::rofi_password`.
- **`syn-popup-lib.zsh`**: one function, `syn_popup::run`, which execs
  a centered, undecorated `foot` window running whatever command you
  pass it, and only pauses for a keypress if that command exits
  non-zero. The undecorated look comes from a window rule in `rc.xml`
  matching on this window's app-id, not from anything in this script.

### Pipe menus

Labwc menus generated live by running a script, not fixed XML.

- **`syn-pipe-docs.zsh`**: lists every doc under
  `/usr/share/syn-os/docs/`, this page included, plus one labeled
  section per topic subdirectory.
- **`syn-pipe-theme.zsh`**: lists every `.theme` file under
  `~/.config/syn-os/themes/`, nested dark/light → family → palette,
  marks whichever one is active.
- **`syn-pipe-display.zsh`**: built from live `wlr-randr` output, one
  submenu per connected screen with power/layout/scale/rotation
  controls. Reads and writes `~/.config/syn-os/disabled-outputs` and
  `~/.config/syn-os/display-layout` so layout survives a reboot.
- **`syn-pipe-share.zsh`**: the full SYN-SHARE menu, server and
  client actions for all six protocols.
- **`syn-pipe-blackarch.zsh`**: only reachable once BlackArch tooling
  is enabled, lists whatever's actually installed and opens each in a
  terminal.
- **`syn-pipe-audio.zsh`**: outputs and inputs menus built from
  `syn-audio --list-sinks`/`--list-sources`, live `[DEFAULT]`/`[MUTED]`
  tags included.
- **`syn-pipe-remote.zsh`**: one submenu per `Host` entry in your own
  `~/.ssh/config`, each opening `syn-pipe-remote-apps.zsh <host>`.
- **`syn-pipe-remote-apps.zsh`**: passthrough to
  `syn-relay --list-apps-for <host>`.

### Prompts and toggles

- **`syn-blackarch-toggle.zsh`**: enables or disables BlackArch
  tooling live. Enabling runs BlackArch's own `strap.sh`, a full
  `pacman -Syu`, and installs a curated subset (`set metasploit
  aircrack-ng`, not the full collection), then inserts a menu entry
  for `syn-pipe-blackarch.zsh` directly into `menu.xml`. Disabling
  removes those packages, strips the `[blackarch]` repo, and deletes
  that menu entry.
- **`syn-services-toggle.zsh`**: lists every systemd unit that's
  actually enabled or disabled on the machine right now (via
  `systemctl list-unit-files`), not a fixed shortlist, and offers only
  the one action that makes sense for whatever state it's in.
- **`syn-share-prompt.zsh`**: collects whatever fields a given
  SYN-SHARE subcommand needs (password, IP, path) via rofi, then runs
  it. Called by both the pipe menu and the bar quick menu.
- **`syn-redshirt-prompt.zsh`**: a rofi front end for
  `syn-redshirt.zsh`. Present in the codebase; see the naming
  collision note above for why nothing currently links to it.

### Directory graphing

- **`syn-graphmap.zsh`** (base, not part of this overlay, lives at
  `/usr/lib/syn-os/syn-graphmap.zsh`): recursively graphs a directory
  tree with Graphviz's `dot`, styled in the live theme, output as PNG
  or SVG under `~/Pictures/SYN-GRAPHMAP/`. Takes directory, max-depth,
  and format as positional args, prompting via a picker for whichever
  one is left blank. A rewrite of the project's original `GRAPH.sh`,
  which is kept for reference at `docs/diagrams/src/GRAPH.sh.orig`.
- **`syn-graphmap-quick.zsh`** / **`-full.zsh`** / **`-custom.zsh`**:
  the three menu entries under SYN-GRAPHMAP, fixed depth 2, fixed
  depth 999, and a depth you type in, respectively. Each just calls
  the base script with that depth already filled in.

### Viewing the docs

- **`syn-docs-view.zsh`** (`/usr/local/bin/`): renders one markdown
  file with `glow` in the current terminal, and pops any SVG diagram
  it references as a real image window via `feh`, cascaded so several
  stack without overlapping exactly.

---

## What a theme switch actually does

Every doc that mentions live theming points at this eventually, so
here's the actual mechanism instead of another pointer. Picking a
theme from `syn-pipe-theme.zsh`'s menu runs one command:
**`syn-theme-apply <theme-name>`** (`/usr/local/bin/syn-theme-apply`).
It's the only thing in SYN-OS that touches every themed surface in one
pass.

A theme file (`~/.config/syn-os/themes/<name>.theme`) is a plain shell
snippet setting variables: `SYN_THEME_NAME`, `SYN_THEME_MODE`
(dark/light), `SYN_THEME_FAMILY`, a palette (`SYN_BG`, `SYN_BG_ALT`,
`SYN_PANEL`, `SYN_PANEL_HOVER`, `SYN_ACCENT`, `SYN_ACCENT_DIM`,
`SYN_TEXT`, `SYN_BORDER`, `SYN_URGENT`), plus `SYN_WALLPAPER`,
`SYN_GLYPH` (the bar's fixed per-theme icon), and
`SYN_WAYBAR_POSITION`. `syn-theme-apply` sources it directly and
substitutes those variables into a set of template files under
`/usr/lib/syn-os/theme-templates/`.

The five style families exist mechanically, not just as a label.
For each template `syn-theme-apply` looks for a file three ways, in
order: a one-off override named for this exact theme (`waybar-style.
SYN-OS-MATRIX.css.tmpl`, which is how MATRIX and WIN95 get their extra
hand-tuned detail), then a shared override named for the theme's
family (`waybar-style.SYN-OS-HALO.css.tmpl`, covering every Halo theme
at once), then the plain shared default every other theme falls back
to. That's the whole mechanism behind "five families, each in dark and
light."

What actually happens per app, in order, matches what the script
prints when it finishes:

| App | What gets rewritten | Applies |
|---|---|---|
| Waybar | `~/.config/waybar/style.css`, the `position` key in `config.jsonc`, the glyph file | Live, waybar is killed and relaunched (a plain reload signal left modules out of sync in practice) |
| Mako | `~/.config/mako/config` | Live, via `makoctl reload` |
| Wallpaper | swaybg restarted with `SYN_WALLPAPER` | Live |
| LabWC | an Openbox-format theme dir under `~/.local/share/themes/`, the `<name>` in `rc.xml` | Live, via `labwc --reconfigure` |
| qt5ct / qt6ct | a generated color scheme under `~/.config/qt[56]ct/colors/`, `color_scheme_path` repointed | Next launch only, no live-reload path exists for Qt apps |
| foot | the `[colors-dark]` block in `foot.ini`, plus a `SIGUSR1` | New windows only, existing terminals keep their old colors |
| GTK3 | `~/.config/gtk-3.0/gtk.css` | Next launch only |

The active theme's name lands in `~/.config/syn-os/current-theme`
only at the very end, once everything else has already been rewritten.

---

## The native tool suite

Covered in depth in their own pages, listed here for the map:

| Tool | Lives at | Doc |
|---|---|---|
| syn-connect | `/usr/lib/syn-os/syn-connect` | [Connectivity](./tools/syn-connect.md) |
| syn-relay | `/usr/lib/syn-os/syn-relay` | [Remote machines](./tools/syn-relay.md) |
| syn-sysmon | `/usr/lib/syn-os/syn-sysmon` | [System monitor & logs](./tools/syn-sysmon.md) |
| syn-audio | `/usr/lib/syn-os/syn-audio` | [Audio mixer](./tools/audio.md) |
| syn-filemanager | `/usr/lib/syn-os/syn-filemanager` | [File manager](./tools/syn-filemanager.md) |
| syn-crypter (native) | `/usr/lib/syn-os/syn-crypter` | [Encryption](./tools/syn-crypter.md) |
| syn-wallgen | `/usr/lib/syn-os/syn-wallgen` | [Wallpapers](./tools/syn-wallgen.md) |
| syn-uplink-dialpad | `/usr/lib/syn-os/syn-uplink-dialpad` | [Prompts](./tools/syn-uplink-dialpad.md) |
| syn-bar-core | `/usr/lib/syn-os/syn-bar-core` | [The tone/notification hub](./tools/syn-bar-core.md) |
| syn-bar | `/usr/lib/syn-os/syn-bar` | [The top bar](./waybar.md) |
| syn-iso-builder | `/usr/lib/syn-os/syn-iso-builder` | [Building your own ISO](./build/iso-builder.md) |

---

## Building the ISO: three ways, not one

- **`BUILD-ARCHISO.zsh`** (repo root): the original builder. Flags:
  `--build=<name>` (a named historical edition from
  `build-manifest.json`, resolved and `git archive`-extracted at the
  exact commit), `--commit=<sha>` (same mechanism, any commit),
  `--profile=<path>` (a local directory), `--list-builds` (prints the
  manifest, no root needed). With no flags it shows a numbered menu of
  all of the above.
- **`BUILD-ARCHISO1.zsh`** (repo root): the current default, a
  re-enterable session menu scoped to three targets only, current
  working tree, a commit browsed with `fzf`, or a local profile, no
  named-build manifest support. Re-execs itself under `doas`/`sudo` if
  not already root.
- **`syn-iso-builder`**: the native ncurses TUI version of exactly
  the same three-target design as `BUILD-ARCHISO1.zsh`, reachable from
  the desktop menu with no terminal needed.

---

## Key paths

| Path | What's there |
|---|---|
| `/etc/syn-os/synos.conf` | The install config, `KEY="value"` lines |
| `/etc/syn-os/install.state` | Disk facts written by `syn-pacstrap.zsh`, read by `syn-stage1.zsh` |
| `/usr/lib/syn-os/` | Installer scripts and native tool binaries |
| `/usr/share/syn-os/docs/` | Every doc, including this one |
| `/usr/share/syn-os/docs/diagrams/` | Graphviz sources (`src/`) and rendered SVGs (`svg/`) |
| `~/.config/syn-os/themes/*.theme` | Theme definitions, plain text |
| `/usr/lib/syn-os/theme-templates/` | Per-app templates `syn-theme-apply` renders themes into |
| `~/.config/syn-os/current-theme` | The active theme's name, one line, written last by `syn-theme-apply` |
| `~/.config/syn-os/disabled-outputs` | Screens kept off at boot |
| `~/.config/syn-os/display-layout` | Multi-monitor layout, replayed on login |
| `~/.wallpaper/` | Generated wallpapers, one per theme |
| `~/.ovpn/` | VPN configs syn-connect's VPN tab reads |
| `~/.ssh/config` | Hosts `syn-pipe-remote.zsh` lists |
| `~/syn-share-pull/` | Files pulled in via SYN-SHARE |
| `/srv/syn-share/` | Files shared out via SYN-SHARE |
| `~/Pictures/Screenshots`, `~/Videos/` | Capture output |
| `~/Pictures/SYN-GRAPHMAP/` | Directory graph output |
| `$XDG_RUNTIME_DIR/syn-bar-core.sock` | syn-bar-core's socket |
| `$XDG_RUNTIME_DIR/syn-relay.*` | syn-relay's plain-file connection state |
