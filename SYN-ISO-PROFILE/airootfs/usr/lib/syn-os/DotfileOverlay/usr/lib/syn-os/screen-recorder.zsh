#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                       S C R E E N - R E C O R D E R
#
#   Toggles wf-recorder: first call starts a recording, second call (while
#   one is running) stops it. $1 selects the mode — "full" records a whole
#   output, prompting to pick one if more than one monitor is connected;
#   "region" is an interactive slurp selection of just the picked area.
#   Both modes prompt for a save directory first. Pass a device name as $2
#   to record audio, e.g. alsa_output.pci-0000_00_1f.3.analog-stereo.monitor
#   (no audio by default).
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SCREEN-RECORDER (Capture)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

PIDFILE="$XDG_RUNTIME_DIR/syn-screen-recorder.pid"

if [[ -f "$PIDFILE" ]]; then
  pid="$(<"$PIDFILE")"
  if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid"
    rm -f "$PIDFILE"
    notify-send "Screen recording" "Stopped" 2>/dev/null || true
    exit 0
  fi
fi

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
syn_theme_load

DEFAULT_DIR="$HOME/Videos"
CHOSEN_DIR=$(printf '%s\n' \
  "$HOME/Videos" \
  "$HOME" \
  "$HOME/Desktop" \
  "$HOME/Downloads" \
  | syn_pick::rofi "Save recording to:")
OUT_DIR="${CHOSEN_DIR:-$DEFAULT_DIR}"
OUT_DIR="${OUT_DIR/#\~/$HOME}"
mkdir -p "$OUT_DIR"

mode="${1:-full}"
geometry_args=()
if [[ "$mode" == "region" ]]; then
  region="$(slurp)"
  [[ -z "$region" ]] && exit 0
  geometry_args=(-g "$region")
else
  # Each line is "port-name<TAB>human label", e.g. DP-2<TAB>LG ULTRAWIDE —
  # 3440x1440, so the picker can show a readable name while still passing
  # wf-recorder the port name it needs.
  randr_out="$(wlr-randr | awk '
    /^[^ ]/ { name=$1 }
    /^  Enabled:/ { enabled=$2 }
    /^  Make:/ { $1=""; make=$0; sub(/^ /,"",make) }
    /^  Model:/ { $1=""; model=$0; sub(/^ /,"",model) }
    /current\)/ {
      if (enabled == "yes") {
        disp = (make != "" && make != "Unknown" ? make " " model : "Built-in display")
        print name "\t" disp " — " $1
      }
      name=""; enabled=""; make=""; model=""
    }
  ')"
  # "${(@f)...}" is zsh's line-split-to-array (no mapfile builtin here).
  # Empty output still gives one empty-string element, not zero — the
  # guard keeps that from counting as an output.
  choices=()
  [[ -n "$randr_out" ]] && choices=("${(@f)randr_out}")
  if (( ${#choices[@]} > 1 )); then
    chosen="$(printf '%s\n' "${choices[@]}" | \
      syn_pick::rofi "Record which screen:" -display-columns 2 -display-column-separator '\t' | \
      cut -f1)"
    [[ -z "$chosen" ]] && exit 0
    geometry_args=(-o "$chosen")
  fi
fi

out_file="$OUT_DIR/$(date +%F_%H-%M-%S).mp4"

audio_args=()
[[ -n "${2:-}" ]] && audio_args=(-a "$2")

# preset=fast avoids x264 profile/level limits at high output resolutions.
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
