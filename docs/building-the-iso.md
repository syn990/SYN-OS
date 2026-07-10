# Building the ISO

SYN-OS is a plain [ArchISO](https://wiki.archlinux.org/title/Archiso) profile (`SYN-OS/SYN-ISO-PROFILE/`), built with `mkarchiso`. `BUILD-SYNOS-ISO.zsh` (`SYN-OS/SYN-OS/BUILD-SYNOS-ISO.zsh`) wraps that with one piece of SYN-OS-specific setup: optionally pre-fetching every package for offline install.

## Prerequisites

```bash
sudo pacman -S archiso git
```

## Running it

```bash
git clone https://github.com/syn990/SYN-OS.git
cd SYN-OS/SYN-OS
sudo zsh ./BUILD-SYNOS-ISO.zsh
```

Note the nested path: the build script lives at `SYN-OS/SYN-OS/BUILD-SYNOS-ISO.zsh` relative to the repo root, not at the repo root itself. The `SYN-OS/SYN-OS/` directory is the actual ISO project (profile, build script, output); the outer `SYN-OS/` is the repo root that also holds the README, images, and these docs.

The script must run as root (it checks `$EUID` and refuses otherwise), and it prompts for confirmation before wiping any previous build:

```
This will build a fresh SYN-OS ISO.
Profile: .../SYN-ISO-PROFILE
Output:  .../ISO_OUTPUT
Mode:    minimal
Continue? (y/n):
```

## Build modes

```bash
sudo zsh ./BUILD-SYNOS-ISO.zsh --mode=minimal   # default
sudo zsh ./BUILD-SYNOS-ISO.zsh --mode=full
```

- **`minimal`**: the ISO's local package repo (`synos-local`) is created empty (a placeholder `.db.tar.gz` with zero packages, via `null_glob` so `repo-add` doesn't choke on an empty pkgcache directory). Packages listed in `SYNSTALL` are fetched from Arch mirrors at install time, so you need network access when running `synos-install`.
- **`full`**: runs `pacman -Syw --cachedir "$PKGCACHE" "${SYNSTALL[@]}"` before building, downloading every package in `SYNSTALL` into the ISO's package cache and adding them to a local repo (`synos-local`). This makes the resulting ISO self-contained (installable offline) but significantly larger.

## What the script actually does

1. Cleans previous `WORKDIR`/`ISO_OUTPUT`/`pkgcache` from prior builds.
2. Sources `syn-packages.zsh` to get `SYNSTALL`.
3. If `--mode=full`, pre-fetches all `SYNSTALL` packages into the pkgcache and builds/updates the `synos-local` repo database.
4. Runs `mkarchiso -v -w "$WORKDIR" -o "$OUTPUT" "$PROFILE"`.
5. On success, prints the path to the resulting `.iso` under `ISO_OUTPUT/`.

Any changes you make to `syn-packages.zsh`, the `DotfileOverlay/`, or any of the `syn-*.zsh` installer scripts are picked up automatically on the next build. They're all read directly from the profile tree, and nothing needs regenerating.

## Rebuilding after config changes

There's no incremental build: `BUILD-SYNOS-ISO.zsh` always wipes `WORKDIR`, `ISO_OUTPUT`, and the pkgcache first. A `full` mode rebuild re-downloads every package unless you've preserved `pkgcache/` yourself, so expect a full build to take noticeably longer than `minimal`.
