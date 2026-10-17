# The acoustic monitor (syn-hum)

Your machine, as quiet sound. Leave it on and you'll hear a runaway
process, a burst of network traffic, a busy disk or an error in the logs
without looking at anything. When nothing's happening it's close to
silent, and every so often something faint passes by.

## Turning it on

Click its icon on the bar, between the volume and the CPU. The icon is
your style's: a satellite dish for uplink, a phone with sound coming
from it for phreak, a hooded figure for hacker, a square wave for
harmonic, an anchor for deep, a vinyl record for techno, a microchip
for bytebeat. It's dim while it's off and
lit while it's listening, and a short tone tells you which way it went.
Click it again to turn it off. Hover over it to see which style it's in,
what it's hearing right now (CPU, each core's step, network, disk and
memory), and the last sounds it played with the time of each, so "what
was that?" always has an answer.

Right-click it to watch the numbers live, with each sound printed as it
plays, so you can see what caused it.

## Styles

Pick one from **Preferences > Desktop > Acoustic Monitor** in the main
menu, along with its volume (quiet, normal or loud). A change takes effect
straight away, even while it's playing.

- **uplink** (the default): Uplink at 3 am, the game's interface tones
  over its music, in a long room with a ping-pong echo. A core at work
  runs the password breaker: fast ticks as characters cycle, then a lock
  tone at that core's note. A pegged core swells into a breathing pad.
  When traffic starts you hear the connection go through its bounces and
  "connected"; when it stops, "disconnected"; heavy traffic runs an
  arpeggio underneath. The disk is the file copier's rising blips. When
  memory runs short the trace tracker starts beeping, faster the worse it
  gets. Now and then a slow pad chord swells, like the game's music.
- **phreak**: a phone line heard in a small room. Each core is a digit
  in soft MF tones, so a busy machine sounds like a switchboard ticking
  over; a pegged core breathes a dial tone. Soft modem scraps for the
  network, a relay for the disk, and when memory runs short, bursts of
  the busy signal coming faster.
- **hacker**: a dark, precise system in a tight room. A precise tick and
  a glass glint at each core's note, in a dark scale; a pegged core
  breathes a low minor third. Data shimmer for the network, a low pulse
  for the disk, sonar pings for memory pressure.
- **harmonic**: the harmonic series in a hall. Each core plays soft harp
  notes on its own harmonic of 110 Hz; a pegged core breathes a pad on
  it. Sparkles high on the series for the network, the fundamental
  swelling for the disk, two notes beating for memory pressure.

- **deep**: inside a submarine, and nothing bright at all. Each core is
  a low sonar ping on its own note; a pegged core is the engine's slow
  throb. Distant propeller wash for the network, the hull creaking for
  the disk, pressure groans for memory, and now and then water rushing
  past or another boat's ping far off.
- **techno**: dark techno at 128 bpm, where every sound waits for the
  next sixteenth note, so the machine's noise falls into a groove, and
  the ambience is made of what the system is doing rather than
  decoration. A busy core strikes metal at its own note, harder the
  harder it works. A pegged core brings in the beat on the next bar: one
  pegged core is the kick, a second adds a rolling bass, a third hats, a
  fourth a clank on two and four, so you can count runaways by how full
  the groove is. Static for the network, a machine clank for the disk,
  and when memory runs short, dissonant stabs into the echo while the
  whole mix gets muffled. Underneath, the system's own operations: a
  fine dust of ticks as often as the scheduler switches tasks, a spark
  for each process started (a build is a cloud of them), a rumble under
  the beat while tasks queue for a CPU, and an echo that hangs longer
  while they wait on the disk. Now and then a dark pad swells, its chord
  set by how hard the whole machine is working: a bare fifth at rest,
  minor when working, a flat ninth when busy, a tritone cluster when
  saturated.
- **bytebeat**: the machine as one-line formulas, the 8-bit music made
  from a single expression like `t*(42&t>>10)`. Every sound is a short
  window onto a formula that's been running since the monitor started,
  so you hear it wherever it has got to. Each core has its own formula,
  and a pegged core lets it keep playing. Sierpinski harmony (`t&t>>8`)
  for the network, a crunch of `t*(t>>5|t>>8)` for the disk, and when
  memory runs short a chirp, while every formula loses bits and goes
  grittier. Now and then a famous one plays far off.

All seven are built the same way: short sounds and slow, breathing
pads, in a room, with a ceiling on the output. A busy machine makes the
short sounds come more often; nothing becomes a tone that stays on and
grows with the load, so a game loading chunks or a long build sounds
busier, not louder. Techno's groove is the one held thing that isn't a
pad, but it's still short sounds: the same bar at the same level,
however hard the cores work.

In the same menu, **Hear Every Sound** plays every sound of your style in
turn and names each one as it plays, which is the quickest way to learn
them. **What Each Style Plays** lists them all side by side.

## Reading it

Most of what you'll hear is "how often". A core that's working pings or
blips now and then, more often the harder it works. A core that stays
pegged for more than three seconds holds a note of its own, so a short
spike passes but a runaway doesn't. The network and the disk work the
same way.

About once a minute it reads out your CPU use as a number, in the style's
own language:

- **uplink** plays it as bounce beeps rising up the scale, one per 10%.
  47% is five.
- **phreak** speaks it in MF tones, the way a blue box sent digits: KP,
  the digits, ST. 47% is KP 4 7 ST.
- **hacker** spells it in binary, a high blip for 1 and a low one for 0.
  47% is 0101111.
- **harmonic** plays a rising run of notes, one per 10%. 47% is five.
- **deep** takes depth soundings: a short low ping per 10%. 47% is five.
- **techno** tolls low metal on the eighth notes, one per 10%. 47% is
  five.
- **bytebeat** plays rising saws, `t*8`, `t*10`, `t*12` and on, one per
  10%. 47% is five.

A log error and a new connection from another machine each get a short,
clean sound of their own, the only two that are never part of the
background.

None of the styles use the 2600 Hz tone or keypad tones, which SYN-OS's
own alerts already use, so nothing here can be mistaken for one of those.

## From a terminal

Everything on the bar and in the menu is also a command:

```
syn-hum toggle            on if it's off, off if it's on (what the bar runs)
syn-hum status            on or off, its style, what it's hearing, what it last played
syn-hum styles            every style, and what each signal sounds like in it
syn-hum set style uplink  uplink, phreak, hacker, harmonic, deep, techno or bytebeat
syn-hum set volume quiet  quiet, normal or loud
syn-hum test harmonic     every sound of a style in turn, each named
syn-hum watch             the numbers it's playing, live, and each sound as it plays
```

It lives at `/usr/lib/syn-os/syn-hum`. Its settings are in
`~/.config/syn-os/hum.conf`, and if something seems wrong,
`$XDG_RUNTIME_DIR/syn-hum.log` says what it's been doing.

## How accurate it is

Everything it plays comes from real counters the kernel keeps, read ten
times a second: CPU time per core, how long tasks waited for memory,
packets in and out, how busy each disk is, new connections from other
machines, and errors in the logs; for techno also processes started,
context switches, and how long tasks waited for a CPU and on IO. Nothing is guessed or predicted.

What it does with them is round them on purpose. Each level is averaged
over about half a second and turned into one of 16 steps (for the CPU,
about 7% a step), so the sound changes in steps you can learn rather
than flickering with every tenth of a second.

Some sounds are exact: one for every log error, one for every new
connection from another machine, the trace tracker's speed, a core
holding its note after 3 s pegged, the readout (your CPU use to the
nearest 10%). The sounds that happen "now and then" are random in their
timing, like a Geiger counter: how often they come follows the load
exactly on average, but no single one is a measurement. The atmosphere
means nothing at all.

However busy the machine gets, the output is held under a ceiling: past
-22 dBFS it's pressed down, and nothing gets past -14.

It costs very little: under 1% of one CPU core to read the machine, and
about 5% of one core while uplink is actually playing (a tenth of a
percent when it's off). A sound reaches you about half a second after
what caused it.

## How it fits together

syn-hum reads the numbers ten times a second: CPU per core, memory
pressure, network packets, disk activity, new connections and log errors.
It never plays anything itself. It sends the numbers to
[syn-bar-core](./syn-bar-core.md), which already plays every other sound
in SYN-OS, and syn-bar-core turns them into the style you picked. If
syn-hum stops for any reason, the sound fades out by itself within two
seconds.

Headphones help. The deepest sounds sit at 110 Hz, which small laptop
speakers barely play.
