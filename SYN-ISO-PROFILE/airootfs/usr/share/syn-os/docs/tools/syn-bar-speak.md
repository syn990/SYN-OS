# Read it aloud (syn-bar-speak)

A button on the bar that reads text to you in a Piper voice: a British
or American voice from upstream, a voice bank of a hundred speakers, or
whatever else is in the machine's voice store. Nothing leaves the
machine and nothing is billed; the voice is made locally by syn-speak,
the same program that voices SYN-CASTER's crew.

SYN-OS ships the program and no voices. The first right-click on the
button offers **Get more voices**, which fetches one.

## Using it

Its icon is a bullhorn, between the volume and the acoustic monitor.

- **Highlight some text, click the button.** It reads the highlight. If
  nothing is highlighted it reads the clipboard instead, so copying
  something and clicking works too. The icon lights up and pulses while
  the voice is reading.
- **Click again to stop**, or middle-click.
- **Right-click for the menu.** Start typing to filter it. At the top:
  read the highlight, read the clipboard, stop. Then the pace (slower,
  normal, faster), **Get more voices**, and every installed voice by
  name, the one in use tagged `[CURRENT]`. Picking a voice says its name
  in that voice, so you hear the change at once. A voice bank (VCTK,
  LibriTTS-R) asks which of its speakers next, listed by number, sex and
  pitch.

Hover for which voice is in use and what each click does.

## Getting voices

**Get more voices** in the menu lists the English Piper voices that
k2-fsa publishes alongside sherpa-onnx (the `tts-models` release on
GitHub): some forty of them, British and American, 63 to 124 MB each,
with the size beside each name. Voices already in the store are left
off the list. Pick one and a popup shows the download; when it's done
the voice is in the store, it becomes the voice in use, and it
introduces itself. The list is fetched from GitHub's API and kept for a
week in `~/.cache/syn-os/speak-catalog.tsv`; without a connection the
cached list still shows, and the download itself says when it fails.

Each fetched voice gets a `VOICE` manifest written for it (its id, its
files, its licence from the model card, where it came from and when), so
syn-speak, SYN-CASTER and SYN-TREK all read it the same way. A voice with
one speaker is an own voice named after it (`alba`, `lessac_high`,
`amy_low`); one with many is a voice bank (`vctk`, `libritts_r`) with a
census of its speaker numbers, their sex and pitch left unmeasured.

The voice store is `~/.local/share/syn-speak/voices`, one folder per
voice. The button lists every voice syn-speak accepts whose manifest
allows the desk. A voice with consent withdrawn, or a broken manifest,
never appears. SYN-CASTER's `docs/SYN-SPEAK.md` has the format.

A machine's own voices, a person's or a character's, are never on that
list and never fetched from anywhere: they are installed by SYN-CASTER's
`voice-install`, and they stay on the machine they were made for.

## The program

syn-speak is built from `SYN-SOFTWARE/syn-speak-src`, a copy of
SYN-CASTER's `tools/syn-speak.c`, and installed as
`/usr/lib/syn-os/syn-speak` with the sherpa-onnx and ONNX Runtime
libraries it needs beside it in `/usr/lib/syn-os/sherpa-onnx`. The build
fetches those two libraries as k2-fsa's prebuilt release (version and
hash pinned, the same ones SYN-CASTER vendors), so nothing ONNX is
installed system-wide and the package depends on nothing but glibc.
`syn-speak-play`, from the same source, turns syn-speak's framed answer
into bare PCM for the player.

The button looks for syn-speak at `SYN_SPEAK_BIN`, then
`/usr/lib/syn-os/syn-speak`, then SYN-CASTER's own `bin/syn-speak` on a
developer machine. Without it the button is dim and its tooltip says so;
with it and an empty store it's dim and says to right-click.

## Settings

The voice, speaker and pace are kept in `~/.config/syn-os/speak.conf`,
three `key value` lines, written whenever the menu changes them. The
same script answers the command line:

```
/usr/lib/syn-os/syn-bar-speak.zsh say [primary|clipboard]
/usr/lib/syn-os/syn-bar-speak.zsh stop
/usr/lib/syn-os/syn-bar-speak.zsh set voice m141
/usr/lib/syn-os/syn-bar-speak.zsh set voice vctk 59
/usr/lib/syn-os/syn-bar-speak.zsh set pace 0.85
/usr/lib/syn-os/syn-bar-speak.zsh voices
/usr/lib/syn-os/syn-bar-speak.zsh catalog
/usr/lib/syn-os/syn-bar-speak.zsh install vits-piper-en_GB-alba-medium
```

`catalog` prints the upstream list (`--refresh` asks GitHub again) and
`install` fetches one by its upstream name, what the menu does behind
the popup.

A pace is a length scale: `1.15` is 15% slower, `0.85` is 15% faster,
`0.5` to `2.0`. What was read, and anything syn-speak refused, is logged
to `$XDG_RUNTIME_DIR/syn-bar-speak.log`.

## How it works

The script reads the Wayland primary selection (the highlight) with
`wl-paste --primary`, falling back to the clipboard, collapses the text to
one line and hands it to `syn-speak --serve` as `voice<TAB>speaker<TAB>
text<TAB>pace`. syn-speak answers with 16 kHz PCM, a frame per sentence
as it is ready, which syn-speak-play unframes straight into PipeWire, so
a long passage starts within a couple of seconds and keeps going while
the rest is made. The
pipeline runs in its own process group so Stop ends all of it at once,
and the button is told with `SIGRTMIN+9` each time it starts, stops or
the voice changes, the way syn-hum signals its own button. An empty
clipboard plays the bar's `FAIL` tone; Stop plays `STOP`.
