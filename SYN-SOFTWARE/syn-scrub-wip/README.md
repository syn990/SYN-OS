# syn-scrub

The reverse of syn-autopsy: removes what a machine knows about you, at
the level you choose, from a plan you've read first. **Not built yet:**
`src/main.c` knows the commands and says so.

## Commands

```
syn-scrub plan user|system [-o PLANFILE]   write the plan and print it
syn-scrub plan disk DEVICE [-o PLANFILE]
syn-scrub run PLANFILE                     carry out exactly that plan
```

`run` takes a plan file, not a level, so what it does is exactly what you
read. Nothing is worked out again between reading and running. A plan
records the machine and the time it was made, and `run` refuses a plan
from another machine or one more than an hour old. WH-UNHACK works the
same way: the plan first, then a yes.

## Levels

**user:** shell history (zsh's, under `$XDG_STATE_HOME`), browser data
(surf, Falkon and Chromium profiles), recently used files, thumbnails,
clipboard history, editor backup and swap files, `~/.cache`.

**system** (as root), on top of `user` for every user: the journal or
svlogd's and syslogd's logs, `known_hosts`, iwd's saved networks and their
keys (`/var/lib/iwd`), VPN profiles, swap (off, overwritten, on again),
`/tmp` and `/var/tmp`, pacman's and xbps's package caches.

**disk:** the whole disk becomes unreadable.

- LUKS (SYN-OS's installer can encrypt): `cryptsetup erase` destroys
  every keyslot, so the data can't be recovered, in a second, with no
  overwrite pass. The plan warns that a copy of the LUKS header kept
  anywhere else would undo this.
- NVMe: `nvme format` with `--ses=2` (crypto erase) where the drive
  supports it, `--ses=1` otherwise.
- SATA: ATA Secure Erase through `hdparm`.
- Anything else: `blkdiscard` where the device supports it, then one
  pass of zeros.

The disk level is meant for the live ISO, against a disk that isn't
mounted. It refuses a mounted disk, and the confirmation is typing the
disk's model and serial, not `y`.

## Rules it keeps

- No run without a plan file.
- Every line of a plan says what it removes, where it is, and how big.
- Levels add up: `system` includes `user` for every user. A `disk` plan
  says the other two are pointless once it's done.

## Files it will have

```
src/main.c
src/syn_scrub_plan.c/.h   building, writing, reading and checking a plan
src/syn_scrub_user.c      one file per level
src/syn_scrub_system.c
src/syn_scrub_disk.c
```

## Build order

1. The plan format, `plan user`, and `run` for it.
2. `system`.
3. `disk`: LUKS first, then NVMe, SATA and the rest. Tested on QEMU disk
   images only, until all of it works.

## Open questions

- **The confirmation.** syn-uplink-dialpad has no yes/no mode and no
  "type this exactly" mode yet (see `../WIP.md`).
- **A handover preset.** "Getting this machine ready to give to someone"
  is `system` plus overwriting free space. That may end up the level used
  most.
