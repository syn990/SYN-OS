# syn-dashcam

A flight recorder for slowdowns. It always holds the last 60 seconds of
what the machine was doing, and when something feels slow you press a key
and it keeps them. You never have to make the slowdown happen again to
see it. **Not built yet:** `src/main.c` knows the commands and says so.

## Commands

```
syn-dashcam start            start recording (daemonizes itself, like syn-relay --start)
syn-dashcam stop
syn-dashcam status [--json]  running or not; --json for waybar
syn-dashcam mark             keep the last 60 seconds (the hotkey runs this)
syn-dashcam list             kept snapshots, newest first
syn-dashcam show SNAPSHOT    what was competing for the machine, worst first
```

## What it records

Four times a second, into a ring of 240 frames in memory:

- `/proc/pressure/cpu`, `memory` and `io`: the kernel's pressure-stall
  counters. They measure how long tasks spent waiting for each resource,
  which is what "slow" is. CPU percentage doesn't tell you that.
- `/proc/stat`: total and per-core CPU.
- `/proc/meminfo`: available memory and swap in use.
- The 10 busiest processes in that frame: `/proc/PID/stat` (CPU time,
  state, page faults) and `/proc/PID/io` (bytes read and written, where
  it's readable).

Nothing touches the disk until `mark`. Recording costs those reads, four
times a second, and nothing else.

## A mark

`mark` writes the ring to `~/.local/state/syn-dashcam/<time>/` as plain
TSV, one file per source, and asks syn-bar-core for a tone so you know it
took. `show` reads it back and ranks what was competing: which resource
was under pressure, from when, and which processes held it.

## Needs

- **`CONFIG_PSI=y`** in the kernel. Arch's kernel has it. SYN-LFS's
  `kernel.syn` doesn't set it, so check that `/proc/pressure/` exists on
  SYN-OS on runit, and add the line if it doesn't. Without it, dashcam
  still records CPU, memory and processes, and says pressure is missing.
- A keybind for `mark` in labwc's `rc.xml`.

## Files it will have

```
src/main.c
src/syn_dashcam_frame.h        one sample: pressure, CPU, memory, top processes
src/syn_dashcam_sample.c/.h    reading /proc into a frame
src/syn_dashcam_ring.c/.h      the 240-frame ring
src/syn_dashcam_daemon.c/.h    start, stop, status, the sampling loop, the socket mark talks to
src/syn_dashcam_snapshot.c/.h  writing and reading a snapshot
src/syn_dashcam_show.c         ranking what was competing
```

Plus `syn_bar_notify.c` from syn-bar-core-src for the tone, reused the way
syn-sysmon does.

## Build order

1. Sampling and the ring, printing frames to stdout.
2. The daemon and `mark`.
3. `show`.
4. A view in syn-sysmon that opens a snapshot, so the hotkey and a click
   on the bar lead to the same place.

## Open questions

- **How much to keep.** 60 seconds at 4 Hz is a starting guess. A short
  spike wants 10 Hz; a slow creep wants 5 minutes at 1 Hz. Possibly two
  rings.
- **A screenshot with each mark**, so you can see what you were doing
  when it went slow.
