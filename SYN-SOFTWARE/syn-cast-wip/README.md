# SYN-CAST

Push-to-talk on Zello channels, as a native SYN-OS window (`syn-cast`) and
an Android app (SYN-CAST-APK, below). Hold space, hear
the beep, talk; let go and it's sent. Everyone on the channel, phones
included, hears it live. What they say plays as it arrives, one message at a
time, and every message stays in the history to replay.

It is the Zello Android app's channel side rebuilt for the desktop: the
feature set, encoder settings and behaviour were taken from the APK
(`docs/REV-zello-7.19.2.md` says what and from where), and it talks to Zello
through the public Channel API, not the app's private protocol.

## What it does

- **Talk**: hold space (or the on-screen bar, or a global key through the
  control socket). Toggle mode if you prefer press-to-start, press-to-stop.
  The talk-permit beep comes when Zello opens the stream, like the phone.
- **Listen**: incoming voice plays live with the talker's name and level;
  messages that overlap (two channels at once) queue and play in turn. Esc
  stops one.
- **History** per channel: voice, texts, images, locations, connection
  events. Enter (or right-click) on a voice message replays it; Ctrl+R
  replays the last one; F2 cycles replay speed 1x / 1.25x / 1.5x with the
  pitch kept.
- **Text**: type and Enter. Everything you send goes to the whole channel:
  the Channel API can only make a message private to someone in the same
  channel, never a true one-to-one, so SYN-CAST sticks to channels. Private
  messages others send you still arrive and are marked as such.
- **Images**: `/img FILE.jpg` sends one (thumbnail first, like the app);
  received ones show inline, Enter opens the full image.
- **Locations**: `/loc LAT LON [ADDRESS]`; Enter on one opens the map.
- **Busy** (F3): incoming voice is kept and counted, not played. **Mute** (F4)
  does that for one channel.
- **VOX** (F5): talking opens the channel, RNNoise decides what talking is.
- **Losses**: the encoder sends in-band FEC like the app does, and lost
  packets are recovered from the next one or concealed.
- **One channel at a time.** Zello allows one session per account and a
  free (Friends & Family) logon joins one channel, so picking another
  channel moves the session there; the rest show NOT JOINED.
- **ONLINE / OFFLINE** (F6, or the button on the phone): offline closes the
  session and keeps the account, so the Zello app or another device can
  have it; online takes it again. If another client signs in with the
  account, SYN-CAST takes it straight back once (the Zello app doesn't fight
  back); a second time within two minutes it goes offline instead of
  fighting.
- **Reconnects** by itself with backoff, using the refresh token Zello gives;
  Zello's keep-alive pings count, so a quiet channel stays connected. A
  refused password is not retried.
- Tones go through syn-bar-core (`START` talk permit, `STOP` end of a
  message, `ALERT` text/image, `FAIL` refused, `TOGGLE_OFF` connection lost).
  Colours follow the live SYN-OS theme, re-read when the window gets focus.

## Commands

```
syn-cast                   the window
syn-cast --headless        no window; the control socket still works
syn-cast setup             write the config (offers SYN-ZELLO's developer key)
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

`~/.config/syn-cast/config.json` (mode 600; it holds a password):

| Key | Default | |
|---|---|---|
| `server` | `wss://zello.io/ws` | |
| `username`, `password` | | the Zello account |
| `issuer`, `key` | | developer issuer and private key (Friends & Family) |
| `auth_token` | | a fixed token instead of issuer + key |
| `channels` | | names; the selected one is joined, picking another moves there |
| `ptt_mode` | `hold` | or `toggle` |
| `auto_play` | `true` | play incoming voice as it arrives |
| `noise_suppression` | `true` | RNNoise on the mic |
| `vox`, `vox_threshold` | `false`, `0.85` | voice-activated talk |
| `mic_gain_db`, `play_gain_db` | `0` | amplifiers |
| `max_talk_seconds` | `300` | |
| `transcriptions` | `false` | ask for `on_transcription` (Work networks) |
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
| `src/zproto.c` | the Channel API as data: commands, events, binary packets |
| `src/ws.c`, `src/jwt.c` | TLS WebSocket, the RS256 auth token |
| `src/audio.c`, `src/stretch.c` | capture, playback, Opus, RNNoise, VOX; replay speed |
| `src/history.c`, `src/image.c`, `src/ctl.c`, `src/config.c` | storage, JPEG, control socket, config |
| `tests/proto_test.c` | unit tests |
| `tests/standin.py`, `tests/e2e.py` | a stand-in Channel API server; two headless instances end to end |

## Build and test

```
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure     # proto (62 checks) and e2e (61)
python3 -I tests/e2e_apk.py build/syn-cast       # the APK's engine against the desktop (41)
```

Needs `wayland libxkbcommon fcft pixman libpulse opus rnnoise libjpeg-turbo
jansson openssl ncurses terminus-font`, and `cmake wayland-protocols tllist`
to build. The end-to-end tests run with `SYN_CAST_NOAUDIO=1` (clocks instead
of sound devices) against `tests/standin.py`; they never touch a live server.

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

```
apk/build.sh          # -> ~/Apps/SYN-CAST/SYN-CAST-APK-0.1.0-personal.apk
adb install -r ~/Apps/SYN-CAST/SYN-CAST-APK-0.1.0-personal.apk
```

The personal APK carries the developer issuer and key from
`~/.config/syn-caster/crew/`, so only the username, password and channels are
asked for; if it ever leaks, revoke the key at developers.zello.com.
`apk/build.sh --shareable` also builds one with no secrets, which signs in
with a pasted or loaded key. Both refuse SYN-CASTER's own account (one
session per Zello account), and a refused sign-in (a wrong password) is not
retried: the form comes back with Zello's reason. The
signing key is `~/.config/syn-cast/apk-signing.jks`, made on the first
build, so new builds install over old ones.

It needs the Android SDK (`platforms;android-36`, `build-tools;36.1.0`,
`ndk;29.0.14206865` under `~/Android/Sdk`). It runs on Android 8 and later.

| File | |
|---|---|
| `apk/java/.../core/` | the engine, protocol, WebSocket, token, history, WSOLA: no Android code, so `tests/e2e_apk.py` runs it on the desktop JVM |
| `apk/java/.../CastService.java` | the foreground service: engine, audio, notifications |
| `apk/java/.../AndroidAudio.java` | AudioTrack, AudioRecord, VOX, the tones |
| `apk/java/.../MainActivity.java`, `Views.java`, `Palette.java` | the screen |
| `apk/jni/` | libopus through JNI |
| `apk/test/` | the headless client the cross test drives |

## Status

Both are built and tested offline end to end, the APK's engine against the
desktop's. Before this folder becomes `syn-cast-src` (WIP.md's "Making one
real"):

- A first sign-in on the live `zello.io` with the owner's account, and a
  talk both ways with a phone. Against the live server only the sign-in
  exchange has been seen so far (it reached Zello and was answered).
- The APK on a real phone: the microphone, speaker, foreground service and
  screen on hardware.
- The packet-id loss check assumes server ids go up by one per packet, as
  the reference implies; confirm it on live traffic.
- labwc menu entry and a talk keybind, and the docs page.
