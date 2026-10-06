# syn-hum

The machine as sound: a quiet, continuous soundscape driven by what the
machine is doing. You hear a runaway process, a port scan or a thrashing
disk before you look at anything. **Not built yet:** `src/main.c` knows
the commands and says so.

## What drives what

| Signal | Read from | Sound |
|---|---|---|
| CPU load | `/proc/stat` | a drone, pitch rising with load |
| Memory pressure | `/proc/pressure/memory` | the drone roughens |
| Network packets | `/proc/net/dev` | clicks, one per N packets |
| Disk IO | `/proc/diskstats` | a low rumble |
| A new inbound connection | `/proc/net/tcp`, `/proc/net/tcp6` | one tone |
| An error in the logs | the journal or svlogd's files (syn-sysmon's readers) | one tone each |

Healthy and idle is close to silence. The mapping lives in
`~/.config/syn-os/hum.conf`, one line per signal, so what drives what is
a plain file you can change.

## Who plays it

SYN-OS has one rule about sound: syn-bar-core is the only thing that
plays any (see `syn-bar-core-src/src/syn_bar_notify.h`). syn-hum keeps
it. syn-hum samples the signals and maps them through `hum.conf`, then
ten times a second sends syn-bar-core one line of voice levels over the
bar's existing socket:

```
HUM drone=0.42 rough=0.03 clicks=12 rumble=0.10
```

One-off events (a new connection, an error) go as tone meanings through
`syn_bar_notify_meaning()`, the way every other tool's sounds do.

That means syn-bar-core needs something new: a continuous voice. Today it
plays each tone through `paplay`, one short sound at a time. A drone that
changes ten times a second needs a stream that stays open (PulseAudio's
`pa_simple`, or PipeWire directly) and a small synthesizer feeding it.
That's the bigger half of this tool, and it lives in syn-bar-core-src.

## Commands

```
syn-hum start | stop | status
syn-hum test     play each voice on its own, so you know what each one means
```

## Files it will have

```
src/main.c
src/syn_hum_sample.c/.h   the signals above
src/syn_hum_map.c/.h      hum.conf: signal -> voice level
src/syn_hum_send.c/.h     HUM lines to syn-bar-core
```

and in syn-bar-core-src, `syn_bar_hum.c/.h`: the voice itself.

## Build order

1. Sampling, printing the signals.
2. The continuous voice in syn-bar-core, driven by a test input.
3. `HUM` lines from syn-hum to syn-bar-core.
4. One-off tones, with new meanings in `syn_tone_vocab.h`. A new inbound
   connection isn't `INTRUSION`, which is reserved for another machine
   polling this one.
5. `hum.conf`.

## Open questions

- **Getting out of the way.** It should probably duck or go silent while
  something else is playing, like a video or a call.
