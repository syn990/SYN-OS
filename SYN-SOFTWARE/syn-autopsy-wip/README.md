# syn-autopsy

A read-only forensic collector. One run gathers a machine's state into
one folder of plain files, hashes every file in it, and merges every
timestamped event into one timeline. **Not built yet:** `src/main.c`
knows the commands and says so.

## Commands

```
syn-autopsy collect [-o DIR]              this machine, live
syn-autopsy collect --root MNT [-o DIR]   a disk mounted at MNT, from the live ISO
syn-autopsy diff BUNDLE                   files that aren't in the baseline, or don't match it
syn-autopsy timeline BUNDLE               the merged timeline, oldest first
syn-autopsy verify BUNDLE                 re-hash the bundle against its manifest
```

## The bundle

`autopsy-<hostname>-<UTC time>/`, readable with `cat` and `less`:

```
manifest.sha256   every file below, hashed as it was written
collect.log       what was read, what couldn't be, and why
system/           kernel, uptime, mounts, users and groups, kernel modules, USB devices seen
autostart/        every place something starts on its own: runit services, systemd units,
                  cron, XDG autostart, shell rc files, labwc's autostart,
                  authorized_keys, doas.conf and sudoers
procs/            processes, command lines, executables, open files (live only)
net/              interfaces, routes, open sockets and who owns them, iwd's known networks
logs/             the journal, or svlogd's and syslogd's files, copied as they are
timeline.tsv      everything above that has a time, one line each, sorted:
                  time, source, event
```

## Rules it keeps

- **Never writes to the machine it's reading.** The bundle goes to `-o`.
  In `--root` mode, mount the disk read-only.
- **Never runs anything from the machine it's reading.** In `--root`
  mode everything is read as files, so a compromised disk's own `ls` or
  `ps` is never trusted.
- **Copies, doesn't interpret.** A log goes in as it was; the timeline
  points at it.
- **Says what it couldn't read.** A permission error goes in
  `collect.log` instead of quietly leaving a hole in the bundle.

## The baseline

`diff` needs a record of what every installed file should be.

- **SYN-OS on runit:** SYN-LFS builds every file itself, so the build can
  write that record: the SHA-256 of every file under `/usr`, `/etc` and
  `/boot`, taken at the end of `build.zsh all` and installed as
  `/usr/share/syn-os/baseline.sha256`. That's a baseline from before the
  machine has ever booted. AIDE and Tripwire are normally set up after
  install, on a machine that's already been running.
- **Arch-based SYN-OS:** pacman keeps a SHA-256 for every file it
  installed, in `/var/lib/pacman/local/*/mtree`. That's the baseline
  there.

`diff` lists files that are missing, changed, or present but in no
baseline, each with its mtime and owner.

## Files it will have

```
src/main.c
src/syn_autopsy_bundle.c/.h   the output folder, writing files, the manifest
src/syn_autopsy_hash.c/.h     SHA-256 through OpenSSL's libcrypto (syn-crypter already links it)
src/syn_autopsy_system.c      one collector per folder above
src/syn_autopsy_autostart.c
src/syn_autopsy_procs.c
src/syn_autopsy_net.c
src/syn_autopsy_logs.c
src/syn_autopsy_timeline.c    merge and sort
src/syn_autopsy_diff.c        compare against the baseline
```

## Build order

1. The bundle, the manifest, and `verify`.
2. `autostart/`, the folder that matters most when something's wrong.
3. `system/`, `net/`, `procs/`, `logs/`.
4. The timeline.
5. `--root` mode.
6. The baseline step in SYN-LFS's build, then `diff`.

## Open questions

- **Windows.** The same bundle from a Windows machine (event logs, Run
  keys, scheduled tasks, services, Defender detections) belongs in
  SYN-WIN as PowerShell, writing the same layout, so one `timeline` reads
  both.
- **Pulling from another machine** over SYN-RELAY waits until relay has
  authentication (see `../WIP.md`).
