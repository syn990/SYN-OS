# syn-boot

A boot timeline for SYN-OS on runit, which has no `systemd-analyze`. It
shows when each part of boot started and how long it took, so the next
second to cut is easy to spot. **Not built yet:** `src/main.c` knows the
commands and says so.

On Arch-based SYN-OS this isn't needed: `systemd-analyze plot` already
does it.

## Commands

```
syn-boot               a table: every step of the last boot, in order, with start and duration
syn-boot --svg [FILE]  the same as a timeline image, themed
```

## Where the times come from

- **Kernel to init.** `btime` in `/proc/stat` is when the kernel started.
  The kernel log line `Run /sbin/init as init process` gives when it
  handed over.
- **Stage 1.** `/etc/runit/1` runs one-shot steps and leaves nothing
  running, so there's nothing to read afterwards. It needs a timestamp
  written before and after each step (`date +%s.%N >> /run/syn-boot/stage1`).
  That's the one change this needs, in `SYN-LFS/runit/1`.
- **Services.** No change to runit. Each service's PID is in
  `/var/service/*/supervise/pid`, and every process's start time is
  field 22 of `/proc/PID/stat`, in clock ticks since boot (10 ms). runit's
  own `sv status` gives uptime in whole seconds, which is too coarse for a
  5-second boot. A service that restarted after boot shows its latest
  start, which stands out as late.
- **Desktop up.** labwc's start time, read the same way, ends the table.

## Files it will have

```
src/main.c
src/syn_boot_kernel.c/.h    btime and the handover to init
src/syn_boot_stage1.c/.h    reading /run/syn-boot/stage1
src/syn_boot_services.c/.h  supervise/pid -> /proc/PID/stat
src/syn_boot_table.c        the text table
src/syn_boot_svg.c          the timeline image
```

## Build order

1. Services from `/proc`. Needs no runit change, so it works on today's
   SYN-OS on runit.
2. The kernel handover.
3. Stage 1's timestamps in `SYN-LFS/runit/1`, then reading them.
4. The SVG.

## Open questions

- **How the image is drawn.** A timeline is bars on a time axis, which
  Graphviz doesn't lay out well, so writing the SVG directly is probably
  right. syn-flow draws the other view of boot, which services start
  what (`syn-flow boot`).
