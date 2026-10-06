# Work in progress

Tools that are designed but not built yet. Each folder ends in `-wip`
instead of `-src`, and that suffix is what keeps it out of every build:
the ISO builders (`BUILD-ARCHISO.zsh`, `BUILD-ARCHISO1.zsh` and the
syn-iso-builder TUI) build every `SYN-SOFTWARE/*-src` folder, and SYN-LFS
builds only the tools named in `desktop/order`. A `-wip` folder is in
neither, so nothing here lands on an ISO half-finished.

Every folder has a `README.md` with the design: what it does, its
commands, the files it will have, the order to build it in, and what's
still undecided. syn-surf starts as surf 2.1 itself. Each of the others
has a `src/main.c` that knows its commands and says it isn't built yet,
and a `CMakeLists.txt` that builds that.

| Folder | What it will be |
|---|---|
| [syn-surf-wip](syn-surf-wip/README.md) | surf 2.1, forked: a capability file per site, a ledger of every request, browsing through another machine, pages saved as Markdown |
| [syn-flow-wip](syn-flow-wip/README.md) | Draws how things work, not where they are: flows from the machine's real state, from tracing a command, or from a sentence |
| [syn-autopsy-wip](syn-autopsy-wip/README.md) | Read-only forensic collector: one hashed bundle of a machine's state and one timeline, live or from a mounted disk |
| [syn-scrub-wip](syn-scrub-wip/README.md) | The reverse of autopsy: removes traces at a chosen level, up to crypto-erasing the disk, always from a plan you've read |
| [syn-dashcam-wip](syn-dashcam-wip/README.md) | Flight recorder for slowdowns: always holds the last 60 seconds, keeps them when you press a key |
| [syn-boot-wip](syn-boot-wip/README.md) | Boot timeline for runit, which has no `systemd-analyze` |
| [syn-lag-wip](syn-lag-wip/README.md) | Keypress-to-frame latency per app, measured |
| [syn-hum-wip](syn-hum-wip/README.md) | The machine as sound: CPU, network, disk and errors as a quiet continuous soundscape through syn-bar-core |
| [syn-fsn-wip](syn-fsn-wip/README.md) | The file system as a 3D picture you turn, zoom and click through: size, type, age and recent changes at a glance |

## Shared work they're waiting on

- **A yes/no mode in syn-uplink-dialpad.** It has `--password`, `--login`
  and `--exec`. syn-surf's capability prompt and syn-scrub's confirmation
  both need a plain question, and syn-scrub also needs "type this exact
  text to continue".
- **A virtual keyboard with a real XKB keymap.** syn-lag needs it to press
  keys; syn-relay needs the same thing for keyboard forwarding. Written
  once, used by both.
- **Authentication in syn-relay.** syn-autopsy pulling from another
  machine, syn-surf browsing through one and syn-fsn showing one all go
  over relay, and none of them should until it has pairing and
  encryption.

## Making one real

When a tool does what its README says:

1. `git mv SYN-SOFTWARE/syn-NAME-wip SYN-SOFTWARE/syn-NAME-src`. The ISO
   builders build it from then on.
2. Add a `PKGBUILD` like the other tools'.
3. Add `SYN-LFS/desktop/pkgs/syn-NAME` (`syn_tool syn-NAME`, the same as
   `pkgs/syn-sysmon`) and put its name in `SYN-LFS/desktop/order` under
   `[synOS]`.
4. Give it a menu entry in labwc's `menu.xml`, and a keybind in `rc.xml`
   if it needs one.
5. Write its page in `SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/`
   and add it to the tool table in the top-level readme. The docs describe
   what's built, so this comes last.
