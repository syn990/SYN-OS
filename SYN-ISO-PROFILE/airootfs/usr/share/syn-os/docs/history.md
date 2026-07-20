# Project History

SYN-OS is one continuous project, not a fork or a successor to something else. It has changed names five times and been reset to a clean baseline more than once, but the repository's contents, its habits, and its intent carry forward across every one of those resets. This document explains where SYN-OS came from, what changed along the way, and — more importantly — what that history means for the system as it exists today.

## Origin: a practice before it was a project

The habits SYN-OS is built from predate any version control by about four years. Hand-building Arch installs without a graphical installer, writing and rewriting dotfiles, compiling custom ISOs — this was personal, repeated manual work going back to roughly 2017, done from memory each time, with no record of what any earlier version looked like. Nothing from that period survives, because nothing was ever committed anywhere.

That changed in 2021, when the practice got its first repository: `SYN-RTOS`, first committed 2021-03-02. The README is explicit about what the repository was *for*, and it wasn't "let's build an OS" — it was "this is getting quite messy," a place to stop losing work that had already been rebuilt from scratch more than once. That framing — a safety net first, ambition second — has held for every reset since.

`SYN-RTOS` was renamed to `SYN-OS` in 2023, and the project has lived under that name since. The two repository names cover one uninterrupted history: files, bugs, and design decisions carry across the boundary rather than stopping at it, and `SYN-OS` itself keeps a snapshot of the old `SYN-RTOS` tree in its own history as a marker of that continuity rather than as a separate origin. Every reset described below — and there have been several — is a restart of the same project, never a fork into something new.

## The first installer, and what it set in motion

The original installer script was 32 lines of shell: one hardcoded disk (`/dev/sda`), one filesystem pairing, GPT partitioning assumed, a single inline `pacstrap` call with a flat package list, no config file, no encryption, no LVM, and no firmware branching — UEFI was simply assumed. Nearly everything that followed in the installer's history is that script growing capabilities it didn't originally have: encryption support, MBR/BIOS handling, firmware auto-detection, a real config file, validation that refuses to continue on a bad combination of settings. Today's installer is roughly an order of magnitude larger in code than that first script, and every added line maps to something the 2021 version genuinely couldn't do.

The early package list is also worth noting because several of its entries are still recognizable in today's SYN-OS, even where the specific tool has since been swapped out — see [Package Identity](#package-identity-what-has-changed-and-what-hasnt) below.

### From X11 to Wayland

For most of its life SYN-OS ran a conventional X11 session: an `.xinitrc` sourcing Xresources/Xmodmap and ending in `openbox-session`, with Openbox's own `autostart` file handling the real daily work — wallpaper, a panel, display geometry. A compositor (`xcompmgr`) was evaluated by hand at one point, tuned, and deliberately rejected; it never made it into the live session, though a leftover reference to it sat unused in `.xinitrc` for years afterward, in a code path (`startx`) that wasn't actually the system's daily entry point.

That entire X11 stack — `.xinitrc`, Openbox, `xcompmgr` — was removed in one pass when SYN-OS moved to Wayland. There's no gradual migration to look at because there wasn't one: the X11 session file was deleted the moment its replacement existed and worked. Today SYN-OS runs [labwc](https://github.com/labwc/labwc) as its Wayland compositor, with no `.xinitrc`, no Openbox, and no `startx` path anywhere in the system. See [labwc](./labwc.md) and [Wayland](./wayland.md) for how the current session is structured.

### How you actually get to a desktop today

SYN-OS doesn't run a display manager. The system autologins as root on the first virtual terminal via a systemd `getty@tty1` override, and a single `synos` shell alias — `dbus-run-session -- env XDG_SESSION_TYPE=wayland labwc` — starts the D-Bus session and execs labwc. That's the entire mechanism: autologin gets you to a shell prompt, one command gets you to a desktop.

This is simpler than it might sound, and simpler than an earlier attempt at the same problem. A complete [greetd](https://sr.ht/~kennylevinsen/greetd/)+[tuigreet](https://github.com/apognu/tuigreet) integration was built at one point — a real installer that set up a dedicated unprivileged greeter user, a full PAM stack, a `greetd.service` unit, and a TUI login prompt in place of a graphical one. It was never wired into the main install flow and was deleted without ever shipping. The TTY-autologin-plus-alias approach that replaced it in practice has no greeter daemon, no dedicated PAM stack, and no seat-managed session to maintain — less machinery, at the cost of the desktop not appearing fully automatically. For a single-user system that's the right trade, which is why it's what SYN-OS actually ships today.

## Shell environment

Zsh has been the target shell since the very first installer scripts, even from a period when the installer scripts themselves were still written in bash. For roughly the first four and a half years of the project, the shell environment vendored the entire [oh-my-zsh](https://ohmyz.sh/) framework directly into the repository — the upstream library, every plugin directory, the custom scaffolding, all committed as source rather than referenced as a dependency, and copied forward unchanged at every project reset.

That framework is gone from the system today. The current `.zshrc` is small (under 200 lines), configures completion and history by hand, and sources plugins — autosuggestions, syntax highlighting — from wherever pacman installs them system-wide, rather than vendoring a plugin framework. The end-user behavior is the same; the mechanism is leaner and has one fewer copy of third-party code living in the repository. `nano` has been the system's default editor across every version of the package lists, from the very first script through today — the one editor choice that's never changed.

See [Zsh](./zsh.md) for the current shell configuration in full.

## The config file's long road to a correct location

The idea of an install-time config file — settings a script reads from, instead of variables edited inline before every run — is almost as old as the project itself, appearing in an early bootloader entry within the first year of `SYN-RTOS`. A fuller version, a `syn.conf` a script actually sourced for hostname, disk target, and locale, existed by mid-2023 and was carried forward through every subsequent project reset in the same flat, ad hoc format, unchanged in shape for roughly two and a half years.

Today's config lives at `/etc/syn-os/synos.conf`, which is where the Filesystem Hierarchy Standard actually says system configuration belongs, and it's read by a dedicated loader, `syn-config.zsh`, that normalizes boolean fields, detects real firmware via `/sys/firmware/efi/efivars`, derives storage-strategy selections from that detection, and hard-fails — not warns — on invalid combinations of settings. The rewrite that fixed the file *format* and the rewrite that fixed its *location* happened in the same window of work, alongside the broader installer overhaul described next. The format took a few years to firm up; the location took considerably longer to become correct. See [synos.conf](./synos-conf.md) for the full current field reference.

## Installer strategy dispatch

For most of the project's history, MBR/BIOS and UEFI handling lived as inline conditionals inside whatever script was doing partitioning at the time — the same basic shape as the original 2021 script, just with a branch added as each new firmware case came up. That approach was explicitly described, partway through the project's life, as "two scripts with enough logic to separate MBR and UEFI properties" — functional, but not a real dispatch system.

The installer was later rewritten around a proper strategy-dispatch model, and that rewrite is the shape the installer still has today. Disk-preparation logic — partitioning, volume management (LVM), filesystem creation, and mounting — lives together in a single file, `syn-disk.zsh`, which exposes internal `partitionMain()`, `volumeMain()`, `filesystemMain()`, and `mountMain()` entry points. Each one dispatches internally on a config-driven strategy string (for example `PartitionStrat`) rather than branching on hardware directly. `pacstrap` is handled by its own separate file, `syn-pacstrap.zsh`. There is no separate `syn-partition.zsh`, `syn-volume.zsh`, `syn-filesystem.zsh`, or `syn-mount.zsh` on disk — disk-prep is one consolidated file with internal dispatch functions, and pacstrap is the one piece that's genuinely its own file.

`syn-config.zsh` resolves an `auto` strategy setting by reading firmware state directly and refuses to continue if an explicit config value contradicts what it detects — a hard exit, not a warning.

This dispatch rewrite is also what made LUKS-on-BIOS installs possible. One MBR strategy has no LUKS support at all and no separate boot partition to fall back on for an encrypted root. A second MBR strategy, `mbr-grub`, exists specifically to close that gap: it carves out a small unencrypted boot partition so GRUB can read its config and load the kernel/initramfs before anything else needs decrypting. This is the one combination that neither the primary UEFI path nor the plain MBR path can cover, and it was added deliberately for that reason — real capability added because the system needed it, not because more options are inherently better. See [Storage Strategies](./storage-strategies.md) for the full partition/volume/LUKS/LVM/`mbr-grub` matrix.

## Package identity: what has changed, and what hasn't

Several packages from that first 2021 script are still directly recognizable in SYN-OS's package lists today, even though many individual tools have been swapped along the way. The most notable case is the browser, which has gone through more churn than anything else in the package lists — several different browsers have held the "default browser" slot over the project's life, with at least one reversal along the way, before settling on Falkon as the current default.

`openra` is the opposite case: it's been present in every version of every package list, across both repository names, continuously since late 2022 — the single longest-lived package entry in the project's history, older than the current terminal, the current editor, the current privilege-escalation tool, and the current file manager.

A number of other tool identities that are true of SYN-OS *today* are the result of deliberate swaps, most of them landing together in one coordinated pass rather than as separate unrelated decisions:

- `htop` → `btop`
- `kitty` → `foot`
- `sudo` → `opendoas`
- `pcmanfm-qt` → `superfile`, later → [syn-filemanager](./tools/syn-filemanager.md) (a purpose-built Qt6 file manager, replacing pcmanfm-qt's unfixed resize bug)
- `kwrite` → `featherpad`
- `engrampa` → `lxqt-archiver`

On the audio side, `pulseaudio` and its Python bindings were part of every package list for roughly the first four years of the project and were dropped outright, with no named replacement sound server ever added to the manifest in their place — whatever sound server actually runs today arrived as a transitive dependency of other packages, not as an explicit choice recorded in the package list. What the manifest does still list explicitly are mixer front-ends and firmware (`pamixer`, `pavucontrol-qt`, `sof-firmware`), not the server itself.

See [Packages](./packages.md) for the current package manifest in full.

## The directory-graphing tool's several rewrites

SYN-OS has had a "draw a picture of the project's own directory tree" tool since fairly early on, and it's been rewritten more times than almost anything else in the repository. The original version was a short shell-plus-awk script that walked the tree and emitted a Graphviz `.dot` file directly. Over time that grew into several overlapping, unconsolidated variants doing roughly the same job in slightly different ways, all of which were eventually deleted together rather than merged.

The habit resumed afterward, twice, close together — one version that still hand-emitted DOT syntax via shell, and a second, independent version that shelled out to real `dot`/`fdp` for actual graph layout. For a short stretch both existed at once, unreconciled, and a shell alias meant to point at the newer one pointed at the wrong filename entirely — a working alias for a script that didn't actually exist under that name.

Today there is exactly one directory-graphing tool: `syn-graphmap.zsh`, living in `usr/lib/syn-os/` alongside the rest of the system's tooling, exposed as a real SYN-OS Tools menu entry with quick/full/custom variants, and the shell alias that invokes it points at a file that's actually there. See [syn-graphmap](./tools/syn-graphmap.md) for the current tool.

## Full project resets

SYN-OS has restarted its own directory tree from a clean baseline several times over its life — not by forking into a new project, but by consolidating everything into one current tree and discarding what came before. Directory-level names that have existed at various points include `SYN-RTOS-OLD`, `SYN-OS-V4`, and `SYN-OS-2035`, all eventually deleted in favor of the single `SYN-OS`/`SYN-ISO-PROFILE` tree that exists today. One particular consolidation point deleted two of those parallel trees in a single stroke, folding several years of divergent work into the one directory structure the project has used ever since.

Release *names* went through even more churn than directory structure. Across the project's public README history, builds have shipped under a series of one-off era names — among them Chronomorph, VOLITION, M-141, and internally-referenced names like SYNTEX, XENITH, SYNAPTICS, and AEGIS — most of them representing a re-upload or relabeling of an existing build rather than a genuinely new fork with its own profile and package list. None of that era-naming survives in the current system: SYN-OS today ships as one unbranded tree, without a rotating release-name scheme layered on top of it.

## What SYN-OS is today, as a result

None of this history is decorative — it's the reason the current system looks the way it does. SYN-OS has grown substantially in capability since its first installer script: encryption, firmware auto-detection, strategy validation, a real config file in the right place, a modular disk-preparation pipeline. Every one of those additions exists because the system needed to actually do the thing, not because more configurability is automatically better.

At the same time, things that stopped earning their place have consistently been removed rather than left to accumulate: the vendored oh-my-zsh framework, the duplicate directory-graphing tools, the greetd/tuigreet path that was built but never wired in, the entire X11 stack once Wayland worked. The project's size has grown where capability genuinely required it, and been cut back everywhere else. That's the throughline connecting a 32-line 2021 shell script to the installer, shell environment, and desktop session SYN-OS ships today: scope grows with real need, weight doesn't get to accumulate just because it's already there.

## See also

- [Philosophy](./philosophy.md)
- [Installer Overview](./installer-overview.md)
- [Storage Strategies](./storage-strategies.md)
- [synos.conf](./synos-conf.md)
- [Packages](./packages.md)
- [Zsh](./zsh.md)
- [labwc](./labwc.md)
- [Wayland](./wayland.md)
- [ISO Builder](./build/iso-builder.md)
