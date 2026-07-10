# Zsh Configuration

SYN-OS has two separate zsh configs, deployed to two different places, for two different purposes. This is worth distinguishing because they look similar but aren't the same file.

## Live-environment shell: `SYN-ISO-PROFILE/airootfs/etc/zsh/zshrc`

This is the ISO's own `/etc/zsh/zshrc`, active the moment you boot the live environment, before any install has happened. It's deliberately minimal and installer-focused:

- Prints the "temporary installer shell" notice and a summary of the two-stage install model on every interactive launch.
- Checks internet connectivity (`curl` against `archlinux.org`) and prints Wi-Fi (`iwctl`) / WWAN (`mmcli`) hints if offline.
- Prints the destructive-install warning and defines `alias synos-install="/usr/lib/syn-os/syn-stage0.zsh"` plus a `confirm_synos-install` function requiring you to type `synos-install YES`, though in practice the alias itself is what gets invoked; the function is a stricter guarded path.
- Sets up completion, history, keybindings, `fzf`/`zoxide`, and the two zsh plugins (autosuggestions, syntax highlighting), the same tooling the README advertises, active even before installation.

None of this reaches the installed system; it only governs the live ISO session.

## Installed-system shell: `DotfileOverlay/etc/skel/.zshrc`

This is what gets deployed to `/etc/skel/.zshrc` (and from there, `~/.zshrc` for any new user) via [Dotfile Overlay](./dotfile-overlay.md) during `pacstrapMain`. This is the shell you actually use day-to-day after install.

Notable aliases and behavior:

| Alias/setting | What it does |
|---|---|
| `synos` | `dbus-run-session -- env XDG_SESSION_TYPE=wayland labwc`: this is **the** command that launches the desktop session; see [First Boot](../readme.md#first-boot) |
| `sudo` → `doas`, `please` → `sudo` | Reflects Stage 1's doas/sudo shim (see [Stage 1](./stage1.md)): `sudo` on the installed system is really `doas` underneath, and this alias makes zsh call it directly rather than through the shim binary |
| `syn-crypter`, `syn-redshirt`, `syn-mapper` | Shortcuts to maintenance scripts under `/usr/lib/syn-os/` |
| `cat` → `bat`, `grep` → `rg` | Conditional aliases, only set if the binary is present |
| `ll`, `la`, `l` | Standard `ls` shorthand |

Plugins loaded (if present): `zsh-autosuggestions` before `zsh-syntax-highlighting` (order matters, since autosuggestions needs to bind first), `fzf` completion + keybindings, `zoxide init zsh`. All three are in [`SYNSTALL`](./packages.md); each `[[ -r ... ]] &&` guard means a missing plugin fails silently rather than erroring on every new shell, but removing one via `pacman` still means losing that feature.

## Editing

Like the desktop configs, only edit `DotfileOverlay/etc/skel/.zshrc` if you want the change to reach newly-installed systems. Editing a live machine's `~/.zshrc` directly only affects that one machine, and editing the live-ISO's `/etc/zsh/zshrc` only affects the installer environment, not what ends up installed.
