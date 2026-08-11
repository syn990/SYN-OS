# The tone and notification hub (syn-bar-core)

Waybar doesn't actually know your CPU usage, your window title, or what
sound to play when something succeeds or fails. It just asks. syn-bar-core
is the small daemon that starts before the bar does and answers those
questions over a socket at `$XDG_RUNTIME_DIR/syn-bar-core.sock`.

## What it tracks

Focused window title, CPU/memory/disk, how many SSH sessions you've got
open, VPN state, and (if you're connected through SYN-RELAY) whatever a
remote machine is reporting too. Waybar's `custom/*` modules just connect
to the socket, ask for a value, print whatever comes back, and exit. All
the actual work happens here.

## One place for every sound

Every native SYN-OS tool that wants to play a sound (connected, failed,
encrypted a file, muted the mic, an ISO build finished) doesn't touch
audio itself. It sends a plain text meaning, like `TONE-MEANING SUCCESS`,
over the same socket and moves on without waiting for a reply. syn-bar-core
is the only thing in the whole system that turns a meaning into an actual
frequency and pipes it to PulseAudio.

| Meaning | Plays when |
|---|---|
| `SUCCESS` | A clean action finished: connected, encrypted, decrypted, built, launched |
| `FAIL` | Something was rejected: wrong password, unreachable host, failed operation |
| `STOP` | An intentional stop: disconnect, stop watching, stop hosting |
| `TOGGLE_ON` / `TOGGLE_OFF` | A persistent state flipped: muted, armed, enabled |
| `ALERT` | Something crossed a threshold on its own, like a hot sensor |
| `KEY_CLICK` | One character typed into a password field |
| `START` | A long-running job began, like an ISO build |
| `STREAM_FAIL` | A remote app stream specifically failed to start |

The upside of doing it this way: if you don't like what `FAIL` sounds
like, there's exactly one file to edit, and every tool that plays `FAIL`
changes with it. Nothing else needs to be rebuilt.

## Watching for relay activity

syn-bar-core also keeps an eye on SYN-RELAY. It reads the state files
SYN-RELAY already writes to `$XDG_RUNTIME_DIR` (no handshake between the
two, just a shared file layout), and uses that to drive the bar's relay
indicators and mirror a connected remote's stats into the bar. Connecting,
disconnecting, and a failed connection attempt each get their own tone.

If another machine starts polling your stats port while you weren't
expecting it, syn-bar-core plays a sharp, distinct alarm and flags it on
the bar. It's modeled on the LanMonitor siren from *Uplink*, the hacking
game this project borrows more than one idea from.

## Nothing to configure here

This one runs in the background and isn't something you open. See [The
top bar](../waybar.md) for what actually shows up, [Connecting to
another machine](./syn-relay.md) for the remote side of things, and
[Password and connection prompts](./syn-uplink-dialpad.md) for the other
regular user of these tones.
