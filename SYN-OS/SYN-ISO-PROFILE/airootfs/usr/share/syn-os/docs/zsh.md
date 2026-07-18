# Zsh Configuration

SYN-OS has two separate zsh configs, deployed to two different places, for two different purposes. They share some structure (history setup, completion, the same two plugins) but they are not the same file and serve different sessions.

## Live-environment shell: `SYN-ISO-PROFILE/airootfs/etc/zsh/zshrc`

This is the ISO's own `/etc/zsh/zshrc`, active from the moment the live environment boots, before any install has happened. It never reaches the installed system — it only governs the installer session. On every interactive launch it:

- Clears the screen and prints a branded splash: a "temporary installer shell" notice, a summary of the two-stage install model (`synos-install` runs `syn-stage0.zsh` pre-chroot; `syn-stage1.zsh` runs automatically inside `arch-chroot` and should never be invoked manually), and a destructive-install warning pointing at `synos-config` / `doas nano /etc/syn-os/synos.conf` for reviewing install parameters first.
- Checks internet connectivity (`curl -fsSIL --max-time 2 https://archlinux.org`) and, if offline, prints hints for `iwctl` (Wi-Fi) and `mmcli` (WWAN modems).
- Defines `alias synos-install="doas /usr/lib/syn-os/syn-stage0.zsh"` and `alias synos-config="doas nano /etc/syn-os/synos.conf"`, plus a stricter `confirm_synos-install()` function that requires typing `synos-install YES` before it will actually run stage0 — in practice the plain `synos-install` alias is what people invoke.
- Sets `EDITOR='nano'` and `LANG=en_GB.UTF-8`, configures history (`$XDG_STATE_HOME/zsh/history`, 50000 entries, `HISTDIR` created if missing), completion (`compinit -i -C -d`, insecure-dirs-ok and skip-function-check since this is a disposable live session), Foot-safe keybinds (Home/End/Delete/Ctrl+Backspace/Ctrl+Delete fixes), `fzf`/`zoxide` integration, and the same two zsh plugins as the installed system (autosuggestions before syntax highlighting).
- Prints a one-line MOTD guarded by `$SYN_MOTD_SHOWN` so it only fires once per session, not on every subshell.

The `doas` prefix on `synos-install` exists because the live session runs as `synstigator`, not `root` — see below.

### The live session runs as `synstigator`, not `root`

Traditional archiso boots straight into a passwordless `root` shell. SYN-OS's live ISO instead autologins as `synstigator`, a wheel-group account created at boot rather than baked statically into the image. The mechanism lives in `airootfs/etc/systemd/system/getty@tty1.service.d/autologin.conf`: an `ExecStartPre` on the tty1 getty unit idempotently creates the account (`useradd -m -G wheel -s /usr/bin/zsh synstigator`), sets a known password (`synstigator:synstigator` via `chpasswd` — intentionally simple, since this is a disposable live session, not the installed system), and grants `permit nopass synstigator` in `/etc/doas.conf` — all on every boot, before the login prompt appears (`agetty --autologin synstigator`).

This has to happen at boot rather than as a static `/etc/passwd`+`/etc/group` entry because `wheel`'s GID isn't fixed in any package — it's allocated dynamically by `systemd-sysusers` the first time the live environment actually boots, so a static file baked into the ISO can't reference a GID it doesn't know yet.

Two consequences worth knowing:
- **SSH works from the moment the ISO boots.** `openssh` is in the live ISO's package set and `sshd.service` is enabled by archiso default; since `synstigator` already has a known password, `ssh synstigator@<ip>` works immediately.
- **`synstigator` never reaches the installed system.** It isn't part of `SYNSTALL`, isn't referenced anywhere in `DotfileOverlay`, and nothing in Stage 0/Stage 1 copies it over. The account you actually get on the installed disk is whatever `UserAccountName` is set to in `synos.conf` (see [synos.conf](./synos-conf.md)); `root` is left exactly as vanilla Arch leaves it, and `sshd.service` is never auto-enabled on the installed system even though `openssh` is installed.

## Installed-system shell: `DotfileOverlay/etc/skel/.zshrc`

This is what gets deployed to `/etc/skel/.zshrc` — and from there, `~/.zshrc` for any user created afterward — via [Dotfile Overlay](./dotfile-overlay.md) during `syn-pacstrap.zsh`'s `pacstrapMain` step. This is the shell used day-to-day after install.

### Environment and history

`EDITOR='nano'`, `LANG=en_GB.UTF-8`. `XDG_CACHE_HOME` and `XDG_STATE_HOME` are set with `${VAR:-default}` fallbacks (`$HOME/.cache`, `$HOME/.local/state`) rather than unconditional exports, so an already-set value from the environment wins. History is persistent and shared across sessions:

```
HISTFILE="$XDG_STATE_HOME/zsh/history"
HISTSIZE=20000
SAVEHIST=20000
```

with `appendhistory`, `sharehistory`, `inc_append_history`, `hist_ignore_dups`, `hist_ignore_space`, `hist_reduce_blanks`, and `hist_verify` set. The directory (`$XDG_STATE_HOME/zsh`) is created with `mkdir -p` before `HISTFILE` is referenced.

### Plugins

Loaded in this order, each behind an existence guard so a missing plugin fails silently instead of erroring on every new shell:

```zsh
[[ -r /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh ]] && \
  source /usr/share/zsh/plugins/zsh-autosuggestions/zsh-autosuggestions.zsh

[[ -r /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh ]] && \
  source /usr/share/zsh/plugins/zsh-syntax-highlighting/zsh-syntax-highlighting.zsh
```

`zsh-autosuggestions` loads before `zsh-syntax-highlighting` deliberately — order matters, since autosuggestions needs to bind first. Both come from [`SYNSTALL`](./packages.md); removing either package still leaves the guard silently skipping it rather than breaking the shell, but the feature itself goes away.

`fzf` (completion + keybindings, from `/usr/share/fzf/`) and `zoxide` (`eval "$(zoxide init zsh)"`) are also loaded conditionally on the binary being present.

### Prompt

Built on `vcs_info` for git status, refreshed in `precmd()`:

```zsh
zstyle ':vcs_info:git:*' formats ' %F{cyan}(%b)%f'
PROMPT='%F{blue}[%f%F{red}%n@%m%f %F{160}%~%f${vcs_info_msg_0_} %F{white}%D{%H:%M:%S}%f%F{blue}]%f %(?.%F{green}✔%f.%F{red}✘%f) %# '
```

user@host in red, working directory in dark red (`160`), git branch in cyan, time in white, brackets in blue, and a trailing success/fail glyph (`✔` green / `✘` red) based on the last command's exit status.

### Aliases

| Alias/setting | What it does |
|---|---|
| `synos` | `dbus-run-session -- env XDG_SESSION_TYPE=wayland labwc` — the command that launches the Wayland desktop session; see [Wayland vs X11](./wayland.md) |
| `sudo` → `doas`, `please` → `sudo` | Reflects Stage 1's doas/sudo shim (see [Stage 1](./stage1.md)): `sudo` on the installed system really is `doas` underneath; this alias calls it directly rather than through the shim binary at `/usr/bin/sudo` |
| `syn-crypter`, `syn-redshirt`, `syn-graphmap` | Shortcuts to `/usr/lib/syn-os/syn-crypter.zsh`, `syn-redshirt.zsh`, `syn-graphmap.zsh` |
| `cat` → `bat`, `grep` → `rg` | Conditional — only set if the binary is present (`command -v ... && alias ...`) |
| `ll`, `la`, `l` | `ls -lah`, `ls -A`, `ls -CF` |

A `mkcd()` function (`mkdir -p -- "$1" && cd -- "$1"`) is also defined, and the file ends by prepending `$HOME/bin` to `PATH`.

### Keybindings and completion

`bindkey -e` (emacs-style), plus explicit fixes for `Ctrl+Backspace` (`backward-kill-word`), `Ctrl+Delete` (`kill-word`), `Delete` (`delete-char`), and history search on `Ctrl+R`/`Ctrl+S`. Completion uses `compinit -d "$XDG_CACHE_HOME/zsh/zcompdump"` with `menu_complete`/`auto_menu` set and Shift-Tab reversed in the completion menu.

## Editing

Like the desktop configs, only edit `DotfileOverlay/etc/skel/.zshrc` if the change should reach newly-installed systems. Editing a live machine's `~/.zshrc` directly only affects that one machine, and editing `SYN-ISO-PROFILE/airootfs/etc/zsh/zshrc` only affects the installer environment, never what ends up installed. See [Dotfile Overlay](./dotfile-overlay.md) for the full deployment path and timing.
