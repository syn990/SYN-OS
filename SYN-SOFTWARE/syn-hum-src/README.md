# syn-hum

The acoustic monitor: the machine as quiet sound. You hear a runaway
process, a burst of traffic, a thrashing disk or an error in the logs
before you look at anything, and when nothing's happening it's close to
silent. A button on the bar turns it on and off; its style and volume are
in the menu. The user's page is
[docs/tools/syn-hum.md](../../SYN-ISO-PROFILE/airootfs/usr/share/syn-os/docs/tools/syn-hum.md).

## Quiet, or sometimes

The first versions were a continuous drone and a hissing line, and idle
was far too loud and too repetitive. What it is now: almost silence, and
now and then something. How often follows the machine. A core that works
pings or blips more often the harder it works; one that stays at step 11
(about 70%) for 3 s holds a voice of its own, so a spike passes and a
runaway doesn't. Network and disk work the same way. About once a
minute the CPU's percentage is read out, and rarely something passes that
carries nothing at all, for the atmosphere.

Every level is a step 0-15, like a redstone signal's strength: the same
load always makes the same sound, and a change is a step you can count.

## Styles

`syn-hum styles` lists what every signal sounds like in each; the table
is `syn-bar-core-src/src/syn_hum_styles.h`, next to the sounds.

- **uplink** (the default): Uplink, the 2001 hacking game this project
  borrows from: its interface tones over its music. The CPU runs the
  password breaker (fast ticks cycling, then a glass lock tone at the
  core's note in A minor pentatonic); a pegged core swells into a saw pad.
  The network connects through its bounce beeps and a two-tone
  "connected" when traffic passes step 5, "disconnected" after 10 s under
  step 3, data blips between and a filtered trance arpeggio while heavy.
  The disk is the file copier, memory pressure the trace tracker (a beep
  every 2 s at step 1, every 0.2 s at step 15) and a tape wobble. The
  readout is rising bounce beeps; the atmosphere is mostly pad chords (A
  minor 9, F major 7). All of it through a dotted-eighth ping-pong echo
  and a Freeverb-style room. The sounds are synthesised in the game's
  manner, not taken from it.
- **phreak**, **hacker** and **harmonic** were rebuilt on uplink's lines
  after the first versions shook the house under load (a 110 Hz drone
  for a busy disk, held sine notes for pegged cores) and, in hacker's
  case, sounded like a cartoon (square-wave blips). Now: phreak is soft
  MF digits per core, modem scraps, a relay, the busy signal for memory,
  heard through the 8 kHz mu-law line and a small room; hacker is a
  precise tick and a glass glint in E phrygian, data shimmer, a low
  pulse, sonar for memory, in a tight room with a slapback; harmonic is
  harp notes on each core's harmonic of 110 Hz, sparkles on the 11th to
  16th, the fundamental swelling for the disk, a beating pair for
  memory, in a hall. Every style's pegged core is a breathing pad.

- **deep**: a submarine, asked for as the one with nothing bright or
  jingly. Low sonar pings per core (G minor pentatonic from G2, with an
  octave so small speakers carry them), the engine's throbbing pad for
  a pegged core, propeller wash (low noise beating at the blade rate)
  for the network, the hull creaking (a low saw bending through a 400 Hz
  low-pass, catching and slipping) for the disk, deeper groans for
  memory, one at a time. Everything goes through a 1.6 kHz low-pass and
  a dark steel room; a faint hull rumble underneath.

- **techno**: dark techno, asked for as ambience tied to performance
  and operations, ORCA and bytebeat the inspiration. Every event waits
  for the grid, a sixteenth at 128 bpm (5625 samples) counted from the
  voice's first sample (`event_new`, the way ORCA's clock bangs its
  operators), so random pings land as a groove. A core's ping is struck
  metal (`SEG_FM`, a sine bent at 1.41 times its pitch, harder with the
  step) on F phrygian from F3. Pegged cores don't each get a pad:
  they're counted, and each brings in a layer of the groove on the next
  bar line (kick, rolling bass, hats, a clank on 2 and 4). A layer is a
  bar long with its last beat 0.11 s short, so the tick after it ends
  starts it again on the very next bar line; layers above the count of
  pegged cores stop at the end of their bar. Network is gated static,
  disk a clank, memory dissonant stabs while a two-pole low-pass on
  everything closes from 20 kHz to 400 Hz with the stall step.
  It's the one style that hears the system's operations, the HUM line's
  `spawn`, `sched`, `cpuwait` and `iowait`: context switches are dust
  (grains a few ms long, 0.4 a second at step 0 to a hundred at 15),
  processes started are sparks (FM glints high on the scale), CPU
  pressure at step 8 for 3 s brings a bar-long rumble under the beat
  (`EV_RUMBLE`), and IO pressure raises the echo's feedback from 0.45
  to 0.78. The grains aren't events (`grains_tick`): at a hundred a
  second they'd take every slot. Its atmosphere isn't random either: a
  pad about every 25 s whose chord is the total CPU (a bare fifth, F
  minor 7, a flat ninth, a tritone cluster).
- **bytebeat**: one-line formulas, a byte a sample on phreak's 8 kHz bus
  (without the phone line). t is the voice's clock, so every window is
  onto one song that's been playing since the voice started; only the
  saws and the chirp take the segment's own time. Each core has a mask M
  in `t*((t>>10&M)|4)` (the 42 melody's shape, the `|4` so a window is
  never all rest), held as the same formula breathing. Every note is a
  harmonic of 31.25 Hz, the byte's wrap at 8 kHz. Memory pressure holds
  t for up to 4 samples and drops up to 3 low bits of every byte. The
  formulas and what each signal plays are `BB_*` in `syn_bar_hum.c`; a
  segment's DC blocker is primed with its first byte, so a formula
  resting on 0 is silence rather than a thump.

Every style is short sounds and breathing pads in a room (`ROOMS` in
`syn_bar_hum.c`; techno's held layers are a bar of drums instead, the
same bar at any load), and nothing holds a tone that grows with the
load: a busy machine makes the short sounds come more often. The output goes
through a 4:1 compressor over -22 dBFS and a limit at -14, so no load
and no volume setting can make it loud.

None of them use 2600 Hz or DTMF, which `syn_tone_vocab.h` already means
things by. A log error and a new inbound connection are the two clean
sounds, outside the line, in every style.

## Who plays it

SYN-OS has one rule about sound: syn-bar-core is the only thing that
plays any. syn-hum keeps it, and has no audio library. Ten times a second
it sends syn-bar-core's socket one line of steps:

```
HUM style=phreak volume=1.00 stall=0 net=3 disk=0 error=0 inbound=0 spawn=2 sched=5 cpuwait=3 iowait=0 cores=1,0,2,1,0,1
```

syn-bar-core's voice (`syn_bar_hum.c`) plays them on a thread of its own,
with one PulseAudio stream that's open only while lines keep coming: two
seconds without one and it fades out, so a syn-hum that died doesn't leave
the machine humming. syn-hum decides what's going on; syn-bar-core
decides what that sounds like, including the now-and-then moments, the
readout and the atmosphere. `HUM-OFF`, `HUM-TOUR` and `HUM-HEARD` are the
other verbs (see `syn_bar_hum.h`).

## Commands

```
syn-hum toggle            start if it's stopped, stop if it's running (the bar's button)
syn-hum start | stop
syn-hum status [--json]   on or off, its style, and what it last played (--json for waybar)
syn-hum styles            every style, and what each signal sounds like in it
syn-hum set style NAME    uplink, phreak, hacker, harmonic, deep, techno or bytebeat
syn-hum set volume LEVEL  quiet, normal or loud
syn-hum test [STYLE]      every sound of a style in turn, each named as it plays
syn-hum watch             print the signals ten times a second, sending nothing
```

Nothing asks anything. `start` puts the daemon in the background (pid in
`$XDG_RUNTIME_DIR/syn-hum.pid`, its own lines in `syn-hum.log` beside
it, and once a second what it's hearing, in words, in `syn-hum.state`,
which `status` and the tooltip show). Every sound syn-bar-core plays is
in `HUM-HEARD`'s list, a run of the same one folded into a line with its
count, and `watch` prints each as it arrives. `start` plays
`TOGGLE_ON` and `stop` plays `TOGGLE_OFF`. Both, and `set`,
send waybar SIGRTMIN+8, which the `custom/hum` module's `"signal": 8`
answers by asking `status --json` again, so the button changes at once.
`set` writes `~/.config/syn-os/hum.conf` and sends a running daemon
SIGHUP to read it.

## Where it's wired in

- waybar: `custom/hum` in the skel `config.jsonc`, beside the volume.
  `status --json` gives the style as `alt`, which `format-icons` turns
  into the style's icon (satellite-dish, phone-volume, user-secret,
  wave-square, anchor, record-vinyl, microchip from Font Awesome), and on or off as `class`. `#custom-hum` is in the module
  groups of `style.css` and every `theme-templates/waybar-style*.css.tmpl`,
  so each theme boxes it like its neighbours, with `.on` and `.off` its
  colours
- labwc: Preferences > Desktop > Acoustic Monitor in both `menu.xml`s,
  `syn-pipe-hum.zsh` (on/off, style, volume, the tour, watch)
- SYN-LFS: `desktop/pkgs/syn-hum`, after syn-sysmon in `desktop/order`
- the ISO builders build it as they build every `-src` folder

## Files

```
src/main.c              commands, and the daemon's loop
src/syn_hum_sample.c/.h the signals, read straight from /proc (pressure from /proc/pressure,
                        processes started and context switches from /proc/stat; the TCP table only when
                        /proc/net/snmp's PassiveOpens moves: reading it walks the
                        kernel's whole connection table, about 0.5 ms a time)
src/syn_hum_map.c/.h    signals -> steps, and the HUM line
src/syn_hum_conf.c/.h   hum.conf
src/syn_hum_send.c/.h   lines to syn-bar-core, and HUM-HEARD's reply
```

From syn-sysmon-src, `syn_stats.c` and the log readers (`syn_journal.c`
on Arch, `syn_journal_files.c` on runit); from syn-bar-core-src,
`syn_hum_styles.h` and `syn_bar_notify.c`. In syn-bar-core-src,
`syn_bar_hum.c/.h` is the voice, built when libpulse-simple is there.

## Open questions

- **Many cores.** The voice gives the first 16 cores a note each; six are
  harmonics 2 to 7, a clean chord. Past about 8 the harmonics crowd
  together and reach the warble's 11th. Pairing cores per voice, or
  folding them into a second octave, are the candidates.
- **A thread that moves.** The scheduler moves a busy thread between
  cores, so a runaway can hop from note to note.
- **Warble that means something.** The modem scraps could carry the
  numbers themselves as real Bell 202 bits, so a modem (or `minimodem`)
  listening to the speaker decodes the machine's state, the way the MF
  readout already can be read by ear.
- **The on and off tones** are SYN-OS's usual `TOGGLE_ON`/`TOGGLE_OFF`,
  louder than anything the monitor itself plays.
