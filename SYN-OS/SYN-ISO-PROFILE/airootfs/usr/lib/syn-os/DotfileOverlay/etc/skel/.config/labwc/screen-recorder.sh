#!/bin/bash
# Toggle wf-recorder: first call starts a recording, second call (while one
# is running) stops it. $1 selects the mode:
#   full          record the whole output, no prompt
#   region        interactive slurp selection, only the picked area
# No audio by default — auto-picking a .monitor source is ambiguous on a
# multi-device setup (grabbed a USB mic's monitor over system audio in
# testing). Pass an explicit device as $2 if you want audio, e.g.:
#   screen-recorder.sh full alsa_output.pci-0000_00_1f.3.analog-stereo.monitor
set -euo pipefail

OUT_DIR="$HOME/Videos"
PIDFILE="$XDG_RUNTIME_DIR/syn-screen-recorder.pid"

mkdir -p "$OUT_DIR"

if [[ -f "$PIDFILE" ]] && kill -0 "$(<"$PIDFILE")" 2>/dev/null; then
  kill -INT "$(<"$PIDFILE")"
  rm -f "$PIDFILE"
  notify-send "Screen recording" "Stopped" 2>/dev/null || true
  exit 0
fi

mode="${1:-full}"
geometry_args=()
if [[ "$mode" == "region" ]]; then
  region="$(slurp)"
  [[ -z "$region" ]] && exit 0
  geometry_args=(-g "$region")
fi

out_file="$OUT_DIR/$(date +%F_%H-%M-%S).mp4"

audio_args=()
[[ -n "${2:-}" ]] && audio_args=(-a "$2")

# preset=fast (not superfast) + no forced profile/level lets x264 pick a
# level that actually fits the resolution — superfast at full desktop
# res hit x264's "MB rate > level limit" warning (forced High/6.2 profile
# couldn't cover 3440x1440 at the encoder's chosen framerate).
wf-recorder \
  "${audio_args[@]}" \
  "${geometry_args[@]}" \
  -c libx264 \
  -x yuv420p \
  -p preset=fast \
  -p crf=23 \
  -f "$out_file" \
  >"$XDG_RUNTIME_DIR/syn-screen-recorder.log" 2>&1 &

echo $! > "$PIDFILE"
notify-send "Screen recording" "Started -> $out_file" 2>/dev/null || true
