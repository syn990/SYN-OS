# Building the ISO

SYN-OS is a plain [ArchISO](https://wiki.archlinux.org/title/Archiso) profile (`SYN-ISO-PROFILE/`), built with `mkarchiso`. `BUILD-SYNOS-ISO.zsh` wraps that with the project's own setup — cleaning up after previous runs, and optionally fetching a real historical build instead of today's working tree.

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

The `SYN-OS/` directory inside the cloned repo is the actual ISO project (profile, build script, output); the outer clone root also holds the README and these docs.

The script must run as root (it checks `$EUID` and refuses otherwise).

## What to build

Run with no flags and you get a numbered menu: build today's mainline working tree, or pick one of the named historical builds. Each historical build is fetched fresh from the real commit history of `syn990/SYN-OS`/`syn990/SYN-RTOS` — the exact profile that actually shipped at that point, not a re-creation. See [Project History](./history.md) for what these builds were.

```
SYN-OS ISO builder — what do you want to build?
  1) Current SYN-OS — this local working tree, uncommitted changes included
  -- Named builds — fetched fresh from real git history, NOT this working tree --
  2) 2021-03-08  SYN-RTOS pre-V3 releng scaffold [fat, 102 pkgs]
  ...
Pick a number (or Ctrl+C to cancel):
```

Or skip the menu directly:

```bash
sudo zsh ./BUILD-SYNOS-ISO.zsh --build=aegis     # a specific named build
sudo zsh ./BUILD-SYNOS-ISO.zsh --list-builds     # print the list, no root needed
```

Either way, it prints what it's about to do and asks for confirmation before touching anything:

```
This will build a fresh SYN-OS ISO.
Profile: .../SYN-ISO-PROFILE
Output:  .../ISO_OUTPUT
Build:   Current SYN-OS — this local working tree, uncommitted changes included
Continue? (y/n):
```

## What the script actually does

1. Cleans up after any previous run: unmounts anything still mounted under `WORKDIR` (an interrupted build can leave `sysfs`/`proc` bind-mounts behind, which would otherwise block the next build), then wipes `WORKDIR`/`ISO_OUTPUT`.
2. For a named build, clones (or reuses a cached mirror of) the relevant source repo, extracts that exact commit's profile into a scratch directory, and patches only what's needed to build on current infrastructure — dropping Arch's retired `[community]` repo block, installing `grub` on the host if that build's `profiledef.sh` actually needs it. Nothing about the build's own install logic or package list is touched.
3. Runs `mkarchiso -v -w "$WORKDIR" -o "$OUTPUT" "$PROFILE"`.
4. On success, moves the resulting `.iso` to `.syncache/isos/<name>.iso` (named after the build, or `mainline.iso` for the working tree) rather than leaving it in `ISO_OUTPUT`, which gets wiped by the next run.

For today's mainline build, any changes you've made to `syn-packages.zsh`, `DotfileOverlay/`, or any `syn-*.zsh` installer script are picked up automatically — they're read straight from the profile tree, nothing needs regenerating.

## Rebuilding after config changes

There's no incremental build: every run wipes `WORKDIR` and `ISO_OUTPUT` first. Named builds cache their source clone under `.syncache/sources/`, so re-running the same `--build=` doesn't re-clone, but `mkarchiso` itself always does a fresh `pacstrap`.
