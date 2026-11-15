# SYN-NODE

Your machines as one control station. Every node is equal: any node can
open the station, see every other node, take its screen, open its shell,
move its files and launch its apps, in both directions. No server
anywhere. Nodes join with a key, talk only to nodes holding a key, and
reach each other directly or through any node that can reach both.

Runs on SYN-OS, other Linux, Windows, macOS and Android. Replaces
syn-relay, which stays as it is until SYN-NODE matches it, then goes.

## Where it's at

**Build step 1 is done:** identity, signed roster, pairing by code,
Noise_IK links, roster gossip, LAN beacons, the control socket, and the
CLI's `pair`, `join`, `nodes`, `status`, `revoke` and `leave`. Not on any
ISO yet (this is still a `-wip` folder); step 2 adds the station, menu
and bar and moves it to `syn-node-src`.

Try it on two machines:

```
cmake -B build && cmake --build build
./build/syn-noded &          # on both machines
./build/syn-node pair        # on one: shows a code
./build/syn-node join 4821 9930   # on the other (finds it on the LAN)
./build/syn-node nodes       # on either
```

`ctest --test-dir build` runs the tests: the Noise test vector, roster
rules, pairing crypto, and three real daemons on this machine pairing,
gossiping, finding each other by beacon, refusing a wrong code, a
stranger with a copied roster and a revoked node, and reconnecting after
a restart.

## What it does

- **Mesh.** Pair a machine once with a short code from any member. It
  joins the whole mesh, not one machine. Every node learns the new key
  from the node that let it in. Revoke a node from any member and the
  revocation reaches all of them.
- **Station.** One view of the whole mesh, opened on whichever node you
  sit at: who's up, CPU, memory, disk, battery, what's running, who's
  watching whom. From any row: Files, Shell, Screen, Apps.
- **Files.** Every node shares the folders you pick. The station shows
  them as one tree, `nodes/<name>/<share>/…`. Copy or move between any
  two nodes. A move between two remote nodes goes straight between them,
  even when you start it from a third. Transfers resume after a break and
  are checked by hash at the end.
- **Shell.** A terminal on any node with a shell (not Android).
- **Screen.** View and control any node's screen. Being viewable is
  per node and per peer, and the bar shows when someone is on yours.
- **Apps.** List and launch a node's apps there, or (Linux to Linux)
  open one here as its own window.
- **Borderless.** Lay the nodes out side by side in the station. The
  pointer leaves one screen's edge and drives the next machine; keyboard
  and clipboard follow it.
- **Routing.** If A can't reach C, traffic goes through B. B forwards it
  still encrypted end to end and can't read it. Nothing to configure.

## Commands

```
syn-node pair                 show a pairing code (lets one machine in)
syn-node join <code>          join the mesh with a code from a member
syn-node revoke <node>        remove a node from the whole mesh
syn-node                      the station (TUI)
syn-node nodes                list nodes, one line each (for scripts)
syn-node files <node>[:path]  station opened on a node's files
syn-node shell <node>         shell on a node
syn-node screen <node>        view/control a node's screen (own window)
syn-node apps <node>          pipe-menu XML of a node's apps
syn-node launch <node> <id>   launch an app there
syn-node send <file> <node>[:path]
syn-node get <node>:<path> [dest]
syn-node borderless on|off
syn-node share add|rm <path> [name]
syn-node stat                 waybar JSON: mesh up / someone on screen
syn-node menu                 labwc pipe-menu of the whole mesh
```

`syn-noded` is the daemon, one per machine (runit service on SYN-OS,
user service elsewhere, foreground service on Android).

## How it's built

```
              station (TUI)   menu.xml   waybar   Android UI (Java)
                     \            |         |        /
                      +---- control socket (local) -+
                                   |
                               syn-noded
                                   |
                         libsynnode (portable C)
        mesh + keys | link (Noise) | channels | routing | files
                                   |
              platform layer: capture, encode, inject, apps, stats
        Wayland/portal | Windows (DXGI, SendInput) | macOS | Android
```

One portable C core, `libsynnode`, in the same shape as SYN-CAST: the
desktop builds it into `syn-noded`, Android loads it over JNI
(`libsynnode.so`) under a Java UI, exactly as SYN-CAST-APK loads
`libsyncast.so`. Windows and macOS build the same core with their own
platform layer.

### Identity and pairing

- Each node has an Ed25519 identity key, made on first start, kept in
  `~/.local/share/syn-node/` (Android: app storage; Windows: `%APPDATA%`).
- The mesh is a **roster**: every member's key, name and who admitted it,
  each entry signed by the admitting member. Revocations are signed
  entries too. Nodes gossip the roster and merge it, so any member's view
  converges.
- Pairing code: 8 characters from the dialpad's alphabet, good for 5
  minutes, once. The code runs **CPace** (a password-authenticated key
  exchange on ristretto255, libsodium) so a code that's overheard or
  guessed wrong once is useless. A successful exchange swaps identity
  keys and the roster. After that the code means nothing.
- Revoked or unknown keys are refused at the handshake.

### Link

- **Noise_IK** handshake (X25519, ChaChaPoly, BLAKE2b, on libsodium)
  between roster members, so the session is authenticated by the identity
  keys both sides already hold. Checked against the Noise test vectors.
- One TCP connection per peer carries the reliable channels, multiplexed:
  control, files, shell, apps, clipboard.
- One UDP flow per peer, under the same keys, carries what must not wait:
  screen video, pointer, keys. Lost packets are skipped, not resent.
- One port for both (TCP and UDP 47900).

### Finding nodes

1. LAN beacon: a small UDP multicast every few seconds, encrypted to the
   mesh, so a non-member sees noise.
2. Last known addresses from the roster.
3. Ask connected peers where a node was last seen.
4. Through a peer: if no direct path works, ask a peer that can reach it
   to forward. Hole punching through that peer comes later.

### Channels

| Channel | Carries | Transport |
|---|---|---|
| control | hello, stats, roster gossip, presence, permissions | TCP |
| files | list, stat, read range, write range, move, delete, hash | TCP |
| shell | a PTY's bytes, window size | TCP |
| apps | list, launch, Linux app-as-window (waypipe stream) | TCP |
| clipboard | text, images, files as references into `files` | TCP |
| screen | H.264 video, cursor shape and position separately | UDP |
| input | pointer, buttons, wheel, keys (with keymap) | UDP, buttons and keys repeated until acked |

The screen channel fixes what made syn-relay's bad:
- capped bitrate with intra refresh, not big periodic keyframes;
- the cursor sent as its own small stream, so it moves at full rate even
  on a still screen;
- frames timed from capture, decoder in low-delay mode;
- input on its own thread, never waiting on video;
- hardware encode where there is some (VAAPI, NVENC, Media Foundation,
  VideoToolbox, MediaCodec), x264 or openh264 otherwise.

### Files

- Shares are named folders, `share add ~/Documents docs`. Nothing outside
  a share is reachable by `files` (the shell can still reach everything;
  that's what a shell is).
- Transfers go in 1 MiB chunks, each with a BLAKE2b hash, written to a
  `.part` file and renamed when the whole-file hash matches. A broken
  transfer resumes from the last good chunk.
- Third-party move: the station tells A to push to B, and B pulls from A.
  The station's machine never carries the bytes.
- Later: the mesh as a real mount (FUSE on Linux, macFUSE, WinFsp), so
  any app can open `~/Mesh/hpaio/docs/…`.
- Later still: synced folders, the same on every node. Conflicts are kept
  side by side, never overwritten.

### Permissions

Per node, per peer, set in the station: files (read / read-write),
shell, screen (view / control), apps, borderless. New members start with
files read, screen view, apps; shell and control are granted, not
assumed. The machine being acted on enforces them, not the one asking.

## Platforms

| | Screen capture | Input injection | Shell | Notes |
|---|---|---|---|---|
| SYN-OS / wlroots | ext-image-copy-capture | wlr-virtual-pointer, virtual-keyboard | PTY | the reference platform |
| GNOME / KDE | xdg-desktop-portal ScreenCast | portal RemoteDesktop | PTY | portal asks the user once |
| Windows | DXGI Desktop Duplication | SendInput | ConPTY | runs in the user's session |
| macOS | ScreenCaptureKit | CGEvent | PTY | needs Screen Recording and Accessibility permissions |
| Android | MediaProjection | AccessibilityService gestures | none | can't receive keyboard/clipboard freely; files through its own share folders and the share sheet |

Android is a full viewer and controller of other nodes. Being controlled
is limited to taps and gestures, which is what Android allows without root.

## The station

A themed ncurses TUI in the same family as syn-sysmon and syn-connect:
`syn_theme` palette from the live theme, Tab between views, a dashboard of
sections rather than one flat list, a status bar of keys at the bottom,
1s tick, never spins.

Views (Tab cycles):

- **Nodes.** "Here" first, then "Online" and "Offline" sections. Each row:
  name, OS glyph, CPU/RAM meters, link (direct / via B, latency), badges
  for "watching you" / "you're watching". Enter opens the node card;
  `f` files, `s` shell, `v` screen, `a` apps.
- **Files.** Two panes, each any node's share (like a two-panel file
  manager). `c` copy, `m` move, `d` delete, `/` filter. Transfers run in
  the background.
- **Transfers.** Live progress, speed, path (direct / routed), pause,
  resume, cancel.
- **Layout.** The nodes' screens as boxes you arrange with the arrow
  keys; this is what borderless follows.
- **Mesh.** Roster, pair (shows the code), revoke, permissions per peer.

Screen view opens its own window (SDL), since a terminal can't show it.
Prompts (codes, confirmations) go through syn-uplink-dialpad like every
other SYN-OS tool.

### menu.xml

The SYN-RELAY menu becomes:

```
SYN-NODE
  Station                     foot -e syn-node
  Nodes  >                    (pipe-menu: syn-node menu)
    hpaio  >
      Files                   foot -e syn-node files hpaio
      Shell                   foot -e syn-node shell hpaio
      Screen                  syn-node screen hpaio
      Apps  >                 (pipe-menu: syn-node apps hpaio)
      Send file here...       (dialpad file pick, syn-node send)
    synbox  >  ...
  Borderless on / off
  Pair a node                 syn-node pair (code in the dialpad popup)
  Join a mesh                 syn-node join (code from the dialpad)
```

Every node shows up under Nodes by itself once it's paired. No IPs typed,
no `~/.ssh/config` needed.

### waybar

One module, `syn-node stat`, reading the daemon's local socket (no
network on each tick): mesh glyph and count online, highlighted when
someone is viewing or controlling this machine. cpu/mem/disk go back to
being this machine's own.

## Files it will have

```
CMakeLists.txt
core/            libsynnode: portable C, libsodium only
  sn_identity.c  keys, storage
  sn_roster.c    signed roster, merge, revoke
  sn_pair.c      CPace pairing
  sn_noise.c     Noise_IK, transport cipher
  sn_wire.c      framing, the wire constants (sn_wire.h)
  sn_mesh.c      mesh secret, address book
  sn_link.c      channel mux, once there's more than control
  sn_discover.c  LAN beacon (in the daemon for now)
  sn_route.c     forwarding through peers
  sn_files.c     shares, chunked transfer, resume, third-party move
  sn_ctl.c       local control socket protocol (station, menu, bar, JNI)
platform/
  linux/         wayland capture/inject, portal, PTY, .desktop apps, stats
  windows/
  macos/
  android/       JNI glue; the Java app lives under SYN-APK/syn-node
daemon/main.c    syn-noded
daemon/sn_daemon.c  the poll loop: links, pairing, beacons, control socket
station/         the TUI (syn_theme, views)
viewer/          screen window (SDL)
cli/main.c       syn-node: subcommands, talks to syn-noded
docs/protocol.md the wire contract, the one source of truth
tests/           Noise vector, roster rules, pairing, test_mesh.sh (real
                 daemons), later transfer resume and routing
```

## Build order

Each step is usable on its own and ends with something you can run.

1. **Core link.** *(done)* Identity, roster, CPace pairing, Noise_IK,
   TCP link, control channel, local socket. Done when: `pair`/`join`
   between the two SYN-OS machines, `syn-node nodes` shows both, a
   tampered or revoked key is refused. Proven on one machine by the
   tests, and between synbox and SYN-HPAIO on 2026-11-15 (join found
   the HP by broadcast, both showed online, reconnected after a restart).
2. **Station + menu + bar.** Nodes and Mesh views, the pipe-menu,
   `syn-node stat`. Done when: the SYN-RELAY menu and its waybar modules
   can be removed with nothing lost for stats.
3. **Files.** Shares, two-pane Files view, transfers with resume,
   third-party move. Done when: a 5 GB file moves between two nodes from a
   third, survives a pulled cable, and the hashes match.
4. **Shell and apps.** PTY channel, app list/launch, Linux app as window.
5. **Screen.** Capture, encode, viewer, input. Done when: it beats
   syn-relay on the same two machines: cursor at full rate on a still
   screen, no smearing after a burst, latency measured with syn-lag.
   syn-relay is retired here.
6. **Borderless.** Layout view, edge crossing, keyboard with keymap,
   clipboard.
7. **Routing.** Forwarding through a peer, then hole punching.
8. **Other platforms.** Android (viewer and files first, built like
   SYN-CAST-APK), then Windows, then macOS.
9. **Mount and sync.** FUSE view of the mesh, then synced folders.

## From syn-relay

Kept, moved into `platform/linux/`: the ext-image-copy-capture code and
the wlr-virtual-pointer injector from `cmd_host.c`, the `.desktop` app
scan (`syn_apps.c`), the stats readers (`syn_stats.c`, `syn_disk.c`).
Kept as dependencies: syn-uplink-dialpad, syn-bar-notify tones.

Dropped: the plaintext stats protocol, the four-requests-per-tick waybar
polling, the separate Connect / Remote / Watch / Host models, IPs typed
into the dialpad, the U/V swap.

## Still undecided

- The name. SYN-NODE is a working title.
- Whether Windows and macOS get the TUI station (it runs in Windows
  Terminal and macOS Terminal) or something native.
- Where shells land on Windows: PowerShell, cmd, or WSL.
- Whether files outside shares are ever reachable through `files`, or
  only through the shell.
- IPv4 only so far. IPv6 links and beacons come with routing (step 7).
- Clock trust: roster times are the signer's clock. A node with a clock
  far in the past could date a revocation early. Fine among your own
  machines; revisit if the mesh ever includes someone else's.
- Hole punching needs a node with a public address in the mesh. If there
  is never one, routed traffic always goes through a member at home.
