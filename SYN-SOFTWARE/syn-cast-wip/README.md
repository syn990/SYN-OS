# SYN-CAST

Push-to-talk on the crew's own radio, as a native SYN-OS window (`syn-cast`)
and an Android app (SYN-CAST-APK, below). Hold space, hear the beep, talk;
let go and it's sent. Everyone on the channel, phones included, hears it
live. What they say plays as it arrives, one message at a time, and every
message stays in the history to replay.

It speaks **SYN-CAST protocol v1**, ours: WebSocket over TLS to SYN-CASTER's
server (`syn-caster cast serve`; SYN-CASTER's `docs/cast-protocol.md` is the
contract), JSON control frames and binary Opus frames, member tokens from a
one-time invite code. Nothing here touches Zello's API. The feature set and
encoder settings came from the Zello Android app's channel side
(`docs/REV-zello-7.19.2.md` says what and from where), rebuilt for the desktop.

## What it does

- **Talk**: hold space (or the on-screen bar, or a global key through the
  control socket). Toggle mode if you prefer press-to-start, press-to-stop.
  The talk-permit beep comes when the server gives the floor (`talk` answered
  with a stream), like the phone.
- **Listen**: incoming voice plays live with the talker's name and level;
  messages that overlap (two channels at once) queue and play in turn. Esc
  stops one.
- **History** per channel: voice, texts, pictures, locations, connection
  events. Enter (or right-click) on a voice message replays it; Ctrl+R
  replays the last one; F2 cycles replay speed 1x / 1.25x / 1.5x with the
  pitch kept. The server keeps every voice message too, as Ogg Opus.
- **Text**: type and Enter. Everything you send goes to the whole selected
  channel. A direct message another member sends you arrives in the selected
  channel, marked private. (The APK gives each direct conversation its own
  tab, @name, and `/msg NAME [TEXT]` opens one; G193.)
- **Pictures**: `/img FILE.jpg` sends one (one JPEG on the wire; the receiver
  cuts its own thumbnail); received ones show inline, Enter opens the full
  picture.
- **Locations**: `/loc LAT LON [ADDRESS]`; Enter on one opens the map.
- **Busy** (F3): incoming voice is kept and counted, not played, and the
  channel is told (presence `busy`); Available plays what was kept. **Mute**
  (F4) does that for one channel.
- **VOX** (F5): talking opens the channel, RNNoise decides what talking is.
- **Losses**: the encoder sends in-band FEC, and lost packets (a gap in the
  frame sequence) are recovered from the next one or concealed.
- **Every channel at once.** `hello` joins every configured channel (or every
  channel you belong to); picking one only aims what you send. One talker per
  channel: a second `talk` is refused `busy` with the talker's name.
- **ONLINE / OFFLINE** (F6, or the button on the phone): offline closes the
  session and keeps the token; online opens it again. A second SYN-CAST
  signing in as the same member on the same device replaces this one (close
  4003): it takes its place straight back once; a second time within two
  minutes it goes offline instead of fighting.
- **Reconnects** by itself with backoff. The server's pings count, so a quiet
  channel stays connected. A refused, expired or revoked token is not retried
  (REFUSED): ask for a new invite.
- Tones go through syn-bar-core (`START` talk permit, `STOP` end of a
  message, `ALERT` text/picture, `FAIL` refused, `TOGGLE_OFF` connection lost).
  Colours follow the live SYN-OS theme, re-read when the window gets focus.

## Commands

```
syn-cast                   the window
syn-cast --headless        no window; the control socket still works
syn-cast setup             write the config (server, invite code, channels, certificate)
syn-cast decode IN.spt OUT.wav

syn-cast ptt down|up|toggle     syn-cast say FILE.wav     syn-cast text MESSAGE
syn-cast image FILE.jpg         syn-cast loc LAT LON [ADDRESS]
syn-cast channel NAME          syn-cast replay | stop
syn-cast busy | mute | vox | speed      syn-cast offline | online      syn-cast status | quit
```

The second group drives the running instance over
`$XDG_RUNTIME_DIR/syn-cast.sock`. A global talk key in labwc's `rc.xml`:

```xml
<keybind key="F9"><action name="Execute" command="syn-cast ptt toggle" /></keybind>
```

Keys in the window: Space talk (when the input is empty; Ctrl+Space always),
Enter send / replay the selected message, Up/Down select, PgUp/PgDn scroll,
Esc stop playback, Tab next channel, Ctrl+1..9 a channel, Ctrl+R replay last,
F2 speed, F3 busy, F4 mute, F5 VOX, F6 online/offline, Ctrl+V paste, Ctrl+C copy,
Ctrl+Q quit.

## Config

`~/.config/syn-cast/config.json` (mode 600; it holds the member token):

| Key | Default | |
|---|---|---|
| `server` | | `wss://HOST:PORT/cast/v1` |
| `invite` | | a one-time code from `syn-caster cast invite MEMBER`; spent on the first hello |
| `token` | | the member token the invite earned (written by syn-cast itself) |
| `member` | | the member id the server answered with (written by syn-cast itself) |
| `device` | `window` | this session's device name; one session per member and device |
| `cert` | | the server's own certificate (PEM path), pinned: it alone is trusted and the host name is not checked; empty trusts the system store |
| `channels` | | ids; empty joins every channel you belong to |
| `ptt_mode` | `hold` | or `toggle` |
| `auto_play` | `true` | play incoming voice as it arrives |
| `noise_suppression` | `true` | RNNoise on the mic |
| `vox`, `vox_threshold` | `false`, `0.85` | voice-activated talk |
| `mic_gain_db`, `play_gain_db` | `0` | amplifiers |
| `max_talk_seconds` | `300` | (the server ends a floor at 5 minutes too) |
| `notify`, `tones` | `true` | notify-send for texts when unfocused; syn-bar-core cues |
| `history_days`, `history_mb` | `30`, `512` | per channel; 0 = no limit |
| `font`, `width`, `height` | `Terminus:size=14`, 980, 640 | |

History lives in `~/.local/share/syn-cast/<channel>/`: `index.jsonl`, voice
as `v<id>.spt` (the Opus packets as received, lost ones marked), images as
`i<id>.jpg` and `i<id>.t.jpg`.

## Files

| File | |
|---|---|
| `src/main.c` | commands, the poll loop, `setup`, `decode` |
| `src/app.c` | sessions, talk button, incoming streams, playback queue, history |
| `src/ui.c` | the Wayland window (pixman + fcft) |
| `src/zproto.c` | SYN-CAST protocol v1 as data: requests, replies, events, binary frames |
| `src/ws.c` | TLS WebSocket, with the server's certificate pinned |
| `src/audio.c`, `src/stretch.c` | capture, playback, Opus, RNNoise, VOX; replay speed |
| `src/history.c`, `src/image.c`, `src/ctl.c`, `src/config.c` | storage, JPEG, control socket, config |
| `tests/proto_test.c` | unit tests |
| `tests/e2e.py`, `tests/e2e_apk.py` | two headless instances (and the APK's engine on the JVM) end to end on a protocol v1 server: `syn-caster` on PATH (or `$SYN_CASTER`), or SYN-CASTER's `tests/castd_ref.py` (`$CASTD_REF`); skipped without one |

## Build and test

```
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure     # proto (79 checks) and e2e (58)
python3 -I tests/e2e_apk.py build/syn-cast       # the APK's engine against the desktop (59)
```

Needs `wayland libxkbcommon fcft pixman libpulse opus rnnoise libjpeg-turbo
jansson openssl ncurses terminus-font`, and `cmake wayland-protocols tllist`
to build. The end-to-end tests run with `SYN_CAST_NOAUDIO=1` (clocks instead
of sound devices) against a real protocol v1 server on loopback, wss with a
self-signed certificate the clients pin: `syn-caster` on PATH (or
`$SYN_CASTER`), else SYN-CASTER's `tests/castd_ref.py` (`$CASTD_REF`, needs
the `websockets` module). Without either they exit 77 and ctest reports them
skipped. They never touch a live server.

## SYN-CAST-APK

The same client rewritten for Android, in `apk/`: plain Java on the Android
framework (no AndroidX, no Gradle), with libopus compiled in through the NDK
so the phone encodes exactly as the REV found the Zello app does (16 kHz,
60 ms, VOIP, VBR, in-band FEC, 20% loss, complexity 10). It does what the
window does: channel tabs, hold or tap to talk, the talk-permit beep, live
playback with a queue, history with replay at 1x/1.25x/1.5x, texts, images
from the gallery, your location, Busy, mute, VOX, ONLINE/OFFLINE, the SYN-OS tones, and all
63 SYN-OS themes (the one live on the building machine is the default). A
foreground service keeps the channel playing with the screen off; the
volume-down key can be the talk button.

Who spoke is drawn as the web chat draws it (G228): each line's name in the
speaker's colour, with a bar of that colour down its left edge. When the bot
speaks for a crew member (protocol `as`), the line carries that member's name
and colour, not the bot's. While a note plays, live or replayed, the speaker's
portrait shows top right over the messages, with their name under it in their
colour; it is fetched once per speaker with the `portrait` request, and a
speaker with no picture gets their initials on their colour.

```
apk/build.sh          # -> ~/Apps/SYN-CAST/SYN-CAST-APK-0.5.0.apk
adb install -r ~/Apps/SYN-CAST/SYN-CAST-APK-0.5.0.apk
```

The APK carries no secrets. On first start it asks for the server, the
one-time invite code (`syn-caster cast invite MEMBER`), the channels (empty:
every one you belong to) and, for a self-signed server, its certificate as
PEM text (pasted or loaded from a file; pinned). The member token the invite
earns is written to the app's own config and used from then on; a refused,
expired or revoked token is not retried: the form comes back with the
server's reason. The signing key is `~/.config/syn-cast/apk-signing.jks`,
made on the first build, so new builds install over old ones.

It needs the Android SDK (`platforms;android-36`, `build-tools;36.1.0`,
`ndk;29.0.14206865` under `~/Android/Sdk`). It runs on Android 8 and later.

| File | |
|---|---|
| `apk/java/.../core/` | the engine, protocol, WebSocket (pinned TLS), history, WSOLA: no Android code, so `tests/e2e_apk.py` runs it on the desktop JVM |
| `apk/java/.../CastService.java` | the foreground service: engine, audio, notifications |
| `apk/java/.../AndroidAudio.java` | AudioTrack, AudioRecord, VOX, the tones |
| `apk/java/.../MainActivity.java`, `Views.java`, `Palette.java` | the screen |
| `apk/jni/` | libopus through JNI |
| `apk/test/` | the headless client the cross test drives |

## Status

Both speak SYN-CAST protocol v1 and are tested end to end against SYN-CASTER's
C server (`syn-caster cast serve`), the APK's engine against the desktop's:
hello with a token or an invite, every channel joined at once, talk/over with
busy, say, pictures, locations, presence (Busy), replay, replacement (4003),
refused and revoked tokens. Before this folder becomes `syn-cast-src`
(WIP.md's "Making one real"):

- The APK on the real phone against the live server (G111 J25 step A's
  install waits for the phone to be on the LAN for wireless ADB), then away
  through SYN-Link's cast stream and the off-network test with its date
  (step B).
- `history`: the APK fetches what it missed (texts and locations) for every
  room and direct message when it signs in, matched on the server's message
  ids it now keeps (G193); voice and pictures missed while away stay on the
  server. The desktop client does not fetch yet.
- The server-side replay (`history` with `play`) is not used: replay is local,
  from the stored voice files.
- labwc menu entry and a talk keybind, and the docs page.
