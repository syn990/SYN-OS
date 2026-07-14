# Zsh Configuration

SYN-OS has two separate zsh configs, deployed to two different places, for two different purposes. This is worth distinguishing because they look similar but aren't the same file.

## Live-environment shell: `SYN-ISO-PROFILE/airootfs/etc/zsh/zshrc`

This is the ISO's own `/etc/zsh/zshrc`, active the moment you boot the live environment, before any install has happened. It's deliberately minimal and installer-focused:

- Prints the "temporary installer shell" notice and a summary of the two-stage install model on every interactive launch.
- Checks internet connectivity (`curl` against `archlinux.org`) and prints Wi-Fi (`iwctl`) / WWAN (`mmcli`) hints if offline.
- Prints the destructive-install warning and defines `alias synos-install="doas /usr/lib/syn-os/syn-stage0.zsh"`, plus a `confirm_synos-install` function requiring you to type `synos-install YES` as a stricter, guarded alternative — in practice the plain alias is what gets invoked. The `doas` prefix exists because the live session runs as `synstigator`, not `root` — see below.
- Sets up completion, history, keybindings, `fzf`/`zoxide`, and the two zsh plugins (autosuggestions, syntax highlighting) — the same tooling the README advertises, active even before installation.

None of this reaches the installed system; it only governs the live ISO session.

### The live session runs as `synstigator`, not `root`

Traditional archiso boots straight into a `root` shell with no password. SYN-OS's live ISO instead autologins as `synstigator` — a wheel-group account baked into the boot flow, not the installer. The mechanism lives in `SYN-ISO-PROFILE/airootfs/etc/systemd/system/getty@tty1.service.d/autologin.conf`: an `ExecStartPre` on the tty1 getty unit creates the account (`useradd -m -G wheel`), sets its password (`synstigator`, intentionally simple and known since this is a disposable live session, not the installed system), and grants it `permit nopass synstigator` in `/etc/doas.conf` — all idempotently, on every boot, before the login prompt appears.

This has to happen at boot time rather than as a static `/etc/passwd`+`/etc/group` entry baked into the ISO image, because `wheel`'s GID isn't fixed in any package. It's allocated dynamically by `systemd-sysusers` (`g wheel - - -` in `/usr/lib/sysusers.d/basic.conf`) the first time the live environment boots — a static file can't reference a GID it doesn't know yet.

Two consequences worth knowing:
- **SSH works from the moment the ISO boots.** `openssh` is in the live ISO's `packages.x86_64` and `sshd.service` is enabled by archiso default; since `synstigator` already has a known password, `ssh synstigator@<ip>` works immediately, no `passwd root` dance required first.
- **`synstigator` never reaches the installed system.** It's not in `SYNSTALL`, its account isn't in any `DotfileOverlay` file, and nothing in Stage 0/Stage 1 copies it over. The account you actually get on the installed disk is whatever `UserAccountName` says in `synos.conf` (see [synos.conf](./synos-conf.md)) — `root` itself is left exactly as vanilla Arch leaves it (no usable password until someone deliberately runs `passwd root`), and SSH on the installed system stays opt-in: `openssh` is installed via `SYNSTALL` but `sshd.service` is never auto-enabled.

## Installed-system shell: `DotfileOverlay/etc/skel/.zshrc`

This is what gets deployed to `/etc/skel/.zshrc` (and from there, `~/.zshrc` for any new user) via [Dotfile Overlay](./dotfile-overlay.md) during `pacstrapMain`. This is the shell you actually use day-to-day after install.

Notable aliases and behavior:

| Alias/setting | What it does |
|---|---|
| `synos` | `dbus-run-session -- env XDG_SESSION_TYPE=wayland labwc`: this is **the** command that launches the desktop session; see [First Boot](../../../../../../../readme.md#first-boot) |
| `sudo` → `doas`, `please` → `sudo` | Reflects Stage 1's doas/sudo shim (see [Stage 1](./stage1.md)): `sudo` on the installed system is really `doas` underneath, and this alias makes zsh call it directly rather than through the shim binary |
| `syn-crypter`, `syn-redshirt`, `syn-graphmap` | Shortcuts to maintenance scripts under `/usr/lib/syn-os/` |
| `cat` → `bat`, `grep` → `rg` | Conditional aliases, only set if the binary is present |
| `ll`, `la`, `l` | Standard `ls` shorthand |

Plugins loaded (if present): `zsh-autosuggestions` before `zsh-syntax-highlighting` (order matters, since autosuggestions needs to bind first), `fzf` completion + keybindings, `zoxide init zsh`. All three are in [`SYNSTALL`](./packages.md); each `[[ -r ... ]] &&` guard means a missing plugin fails silently rather than erroring on every new shell, but removing one via `pacman` still means losing that feature.

## Editing

Like the desktop configs, only edit `DotfileOverlay/etc/skel/.zshrc` if you want the change to reach newly-installed systems. Editing a live machine's `~/.zshrc` directly only affects that one machine, and editing the live-ISO's `/etc/zsh/zshrc` only affects the installer environment, not what ends up installed.
