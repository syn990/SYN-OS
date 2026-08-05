# SYN-RELAY protocol

What's actually built and running, not a design aspiration. (An earlier
draft of this doc — written for what was then a separate SYN-AGENT tree —
described a PIN-pairing/HKDF handshake that was never implemented; this
version describes the real, current wire format instead.)

## One binary, four roles

`syn-relay` is a single executable. Each role below is independent —
a machine can run several simultaneously (e.g. serve stats to one peer
while being watched by another) — see `syn_node_state.h`/
`syn_agent_state.h` for how each role's state is tracked separately.

- **Stats client** (`cmd_stats_client.c`) — connects to a remote
  machine's stats server, feeds waybar's cpu/mem/disk modules and
  menu.xml's remote-apps pipe-menu.
- **Stats server** (`cmd_stats_server.c`) — serves this machine's live
  stats/app list to a connecting stats client.
- **Screen watch** (`cmd_watch.c`) — views and controls a remote
  machine's screen.
- **Screen host** (`cmd_host.c`) — lets a remote machine view/control
  this one.

## Stats channel: TCP, port 47991

Text-command-plus-JSON-reply, one request per connection
(`syn_relay_conn.c`'s `syn_relay_request()`):

- `STATS` → `{"hostname":...,"cpu_pct":...,"mem_used_kb":...,"mem_total_kb":...,"disk_used_kb":...,"disk_total_kb":...,"gpu_pct":...|null,"sunshine":{"running":bool,"port":N}}`
- `LIST_APPS` → `{"apps":[{"id":...,"name":...,"icon":...}, ...]}`
- `LAUNCH_APP <id>` → `{"ok":true}` or `{"ok":false,"error":"..."}`

`id` is an opaque, server-platform-defined handle — the client never
parses or constructs it, only stores and echoes it back verbatim in
`LAUNCH_APP`. Today (Linux server) it's a `.desktop` file's absolute
path. A future Windows server could make it a Start Menu shortcut
path or a ProgID; a future macOS server could make it a `.app` bundle
path — no client-side change required either way, since
`cmd_stats_client.c`'s `cmd_list_apps()`/`cmd_launch()` never interpret
`id`, only pass it through.

## Video channel: MPEG-TS over UDP, port 9999

`cmd_host.c`'s video child captures the screen via
`ext-image-copy-capture-v1` (current Wayland capture protocol, not the
deprecated `wlr-screencopy`), encodes H.264 with libx264
(`preset=ultrafast tune=zerolatency`, `repeat-headers=1` so a
late-joining viewer can sync at the next keyframe), and streams it via
FFmpeg's own `mpegts` muxer to `udp://<viewer-ip>:9999`. `cmd_watch.c`'s
child opens a UDP listen socket on the same port, decodes, and displays
via SDL2.

## Input channel: raw UDP, port 10000

Fixed-size binary structs (`input_protocol.h`), no framing, no acks —
mouse motion (absolute, coordinates normalized to 0..65535 so neither
side needs to know the other's actual resolution) and button state,
sent from the watch role back to the host role, injected there via
`wlr-virtual-pointer-unstable-v1`. Keyboard forwarding doesn't exist
yet — would need a real XKB keymap uploaded to the compositor first,
genuinely more involved than pointer events.

## Security posture

**No authentication or encryption anywhere** — not on the stats
channel, not on the video/input channels. Deliberate for v1, matching
this project's existing trusted-LAN-only stance (not a regression
introduced by this merge). Concretely:

- Anyone who can reach port 47991 can read this machine's stats and
  launch arbitrary installed applications via `LAUNCH_APP`.
- Anyone who knows a `--host-watched` session's video/input ports can
  watch that stream and inject mouse input.
- `--connect`/`--watch`/`--host-watched` all take a plain IP/hostname
  with no verification of identity.

Pairing/authentication is real future work, not scoped into this pass.
If added, it would sit at the start of each channel (a shared-secret
handshake before the stats TCP connection completes; something
equivalent gating who a video/input session will send to or accept
from) without changing the wire formats above.

## Build dependencies

`wayland-client`, `wayland-protocols` (for the checked-in generated
code under `protocol/`), FFmpeg (`libavcodec`/`libavformat`/`libavutil`/
`libswscale`), and `sdl2` are required to build this binary at all, even
though only the screen watch/host roles actually use most of them — see
the top-level merge plan for the measured cost of this (dynamic-linking
overhead on every invocation, including plain stats polls) and why it
was accepted anyway.
