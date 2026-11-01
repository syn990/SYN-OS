# REV: Zello for Android 7.19.2

What was learned from the Zello APK to build syn-cast, and where each fact came
from. syn-cast contains no code, sounds, images or text from the APK: it is
written from these notes and the public Channel API reference.

## The sample

| | |
|---|---|
| File | `~/Apps/zello-android/zello-7.19.2.apk` |
| SHA-256 | `ec88d561be481e131f2dba07e703ab3c520c1a145b6a38f0923b0f935644dad8` |
| Package | `com.loudtalks` (Zello began as LoudTalks), versionCode 7001902, minSdk 24 |
| Tools | `unzip`, `jadx` 1.5.6 (`--deobf`, 37,766 classes, 282 that did not decompile), `objdump`, `strings`, `nm` |
| Working copy | `~/.cache/syn-rev/zello-7.19.2/` (`apk-raw/`, `jadx-out/`, `dex-strings.txt`), kept out of the repo |

To redo it (the box is RAM-tight, so jadx runs fenced and writes to disk, not
to the tmpfs):

```
W=~/.cache/syn-rev/zello-7.19.2; mkdir -p $W && unzip -q zello-7.19.2.apk -d $W/apk-raw
JAVA_OPTS=-Xmx2800m systemd-run --user --scope -q -p MemoryMax=3400M -p MemorySwapMax=0 \
  jadx -j 3 --deobf --deobf-min 3 -d $W/jadx-out zello-7.19.2.apk
```

## What is inside

Native libraries (`lib/x86_64/`, the same set for each ABI), from their
exported `Java_com_zello_*` symbols:

| Library | Job | On Linux |
|---|---|---|
| `libzello.opus.so` | Opus 1.5 encoder/decoder (with DRED), repacketizer | `opus` |
| `libzello.amr.so` | AMR codec, for old clients | not needed |
| `libzello.rnn.so` | RNNoise: `denoise`, `calculateVoiceActivity` (VOX) | `rnnoise` |
| `libzello.soundtouch.so` | time-stretch for replay speed | `src/stretch.c` (WSOLA) |
| `libzello.webrtc.so` | AGC / noise suppression / VAD | not used yet |
| `libzello.util.so` | WAV file reader, sample-rate converter | `src/audio.c` |
| `libvosk.so` | offline speech recognition for voice commands | not ported |
| `libjingle_peerconnection_so.so` | WebRTC, for video calls | not ported |

`assets/` holds the alert sounds (`snd/*.wav`), 41 UI translations
(`lng/*.json`, 2,554 strings in `en.json`), Zello's own root CAs
(`zello-root-ca-*.crt`, `zes-root-ca*.crt`) and a list of supported
Bluetooth PTT buttons (`ble/list.json`).

## Transport: why syn-cast uses the Channel API

The phone app does not speak the Channel API. Its strings name
`tls.zello.com` / `tls.zellowork.com`, its logon reply carries `logon_cookie`,
`licensed`, `host`, a protocol version `v` and an `rsa` key size of at least
512 bits (`p000/wb8.java`), and it pins its own root CAs. That is a private
protocol with its own client handshake.

Rebuilding it would mean posing as Zello's official client: against Zello's
terms, a risk to the account, and broken by any app update. Zello publishes
the **Channel API** for third-party clients (`wss://zello.io/ws`, JSON +
binary WebSocket frames, documented at
github.com/zelloptt/zello-channel-api), and SYN-ZELLO already signs in through
it with the developer key on this machine. syn-cast uses that. The API reaches
channels, not contact lists, so the port is the channel half of the app.

## Audio: the encoder settings

`EncoderOpus.java` defaults: 16 kHz, 60 ms frames, 2 frames per packet
(capped at 120 ms of audio per packet), selectable rates 8/12/16/24/48 kHz.
The bitstream settings are in native code. `objdump -d` of
`Java_com_zello_platform_audio_EncoderOpus_nativeStart` and the function it
calls show:

```
opus_encoder_create(rate, 1, 0x800)            OPUS_APPLICATION_VOIP
opus_encoder_ctl(enc, 0xFA8, -1000)            OPUS_SET_BANDWIDTH(OPUS_AUTO)
opus_encoder_ctl(enc, 0xFA6, 1)                OPUS_SET_VBR(1)
opus_encoder_ctl(enc, 0xFAC, 1)                OPUS_SET_INBAND_FEC(1)
opus_encoder_ctl(enc, 0xFA2, <setting>)        OPUS_SET_BITRATE (0 by default: left on auto)
opus_encoder_ctl(enc, 0xFAA, 10)               OPUS_SET_COMPLEXITY(10)
opus_encoder_ctl(enc, 0xFAE, 20)               OPUS_SET_PACKET_LOSS_PERC(20)
opus_repacketizer_*                            joins 2 frames per packet
```

syn-cast encodes from the 48 kHz capture (RNNoise needs 48 kHz) with the same
settings, capped to wideband at 17 kb/s (what the 16 kHz encoder picks on
auto), and announces `16000 Hz, 1 frame, 60 ms` in `codec_header`. The
in-band FEC matters: in the end-to-end test, with every fifth packet dropped,
the next packet's FEC brings most lost frames back.

## Behaviour

| Fact | Source | In syn-cast |
|---|---|---|
| Talk button states SENDING, IDLE, CONNECTING, RECEIVING, DISABLED | `com/zello/history/TalkButtonState` | `talk_state` in `app.h`, the PTT bar |
| The beep comes when the server opens the stream ("Ready to send", `alert_cts`), not on the press | `en.json` `alert_cts`, `alert_ptt_hold_help_message`: "Hold the button, hear the beep and talk." | `tx_started()` plays the talk-permit tone |
| Replay speeds 1, 1.25, 1.5, cycling | `com/zello/history/HistoryPlayerSpeed` | `app_cycle_speed()`, F2 |
| Playback waits while you record, play history, or are in a call | `com/zello/messages/PlaybackRestriction` | the queue holds while talking |
| Messages end by: user, max duration, interrupt, server, VOX silence... | `com/zello/messages/MessageEndReason` | release, `max_talk_seconds`, VOX hang time |
| Button actions: hold to talk, toggle | `advanced_key_action_type_ptt/_toggle` | `ptt_mode` |
| Busy status: messages are kept, not played | `status_busy`, `alert_incoming_busy` | F3 / `busy` |
| Mute a channel | `status_muted` | F4 / `mute` |
| VOX with sensitivity levels | `vox_sensitivity_*`, `Rnn.calculateVoiceActivity` | F5 / `vox_threshold` |
| Noise suppression, recording and playback amplifiers in dB | `options_audio_noise_suppression`, `advanced_audio_*_amplifier` | `noise_suppression`, `mic_gain_db`, `play_gain_db` |
| History of voice, images, texts, locations, kept by age and size | `options_history_*`, `history_option_after` | `history.c`, `history_days`, `history_mb` |
| "Replay last message" | `menu_replay_last_message` | Ctrl+R / `replay` |
| Message status: sending, sent, failed | `msg_status_*` | a text stays grey until the server confirms it |
| Image review states, thumbnails then the full image | `img_status_*`, Channel API | thumbnail inline, Enter opens the full image |

## Sounds

Zello's cues (`snd/*.wav`) are not shipped. Each cue maps to a SYN-OS tone
meaning that syn-bar-core already plays, so the radio sounds like the rest of
the desktop:

| Zello cue | Measured | syn-bar-core meaning |
|---|---|---|
| `pttup.wav` (talk permit) | 100 ms | `START` |
| `over.wav` (end of incoming) | 150 ms | `STOP` |
| `error.wav`, refused talk | 804 ms | `FAIL` |
| `connection_lost.wav` | 1001 ms | `TOGGLE_OFF` |
| `connection_found.wav` | | `SUCCESS` |
| `texting.wav`, `image.wav`, `alert.wav` | | `ALERT` |

## Not portable over the Channel API

Contacts and one-to-one conversations outside a channel, presence statuses
(Available/Away/Headphones as the server sees them), call alerts and
emergency alerts (the API lists alerts as planned for Work only), video
calls, dispatch consoles (the API has them for Work dispatchers), voice
commands (Vosk), Bluetooth PTT accessories, server-side history download.
Private messages within a channel (`for`) are not sent, by choice: they
reach only someone already in the channel, so SYN-CAST sticks to channels.
Ones that arrive are shown. Transcriptions arrive when the account's
network provides them (`features.transcriptions`).
