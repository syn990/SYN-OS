#!/usr/bin/env zsh
# Toggle wf-recorder: first call starts a recording, second call (while one
# is running) stops it. $1 selects the mode:
#   full          record a whole output — if more than one is connected,
#                 prompts with a plain-language list (monitor make/model
#                 + resolution, not port names like "DP-2") since
#                 wf-recorder with no -o/-g just grabs whatever wlroots
#                 enumerates first, not necessarily the screen you're
#                 looking at
#   region        interactive slurp selection, only the picked area
# Both modes prompt for a save directory first, same as screenshot.zsh.
# No audio by default — auto-picking a .monitor source is ambiguous on a
# multi-device setup (grabbed a USB mic's monitor over system audio in
# testing). Pass an explicit device as $2 if you want audio, e.g.:
#   screen-recorder.zsh full alsa_output.pci-0000_00_1f.3.analog-stereo.monitor
emulate -L zsh
setopt NO_UNSET PIPE_FAIL 2>/dev/null || true

PIDFILE="$XDG_RUNTIME_DIR/syn-screen-recorder.pid"

if [[ -f "$PIDFILE" ]] && kill -0 "$(<"$PIDFILE")" 2>/dev/null; then
  kill -INT "$(<"$PIDFILE")"
  rm -f "$PIDFILE"
  notify-send "Screen recording" "Stopped" 2>/dev/null || true
  exit 0
fi

CURRENT_THEME_FILE="$HOME/.config/syn-os/current-theme"
THEMES_DIR="$HOME/.config/syn-os/themes"
theme_name="SYN-OS-RED"
[ -f "$CURRENT_THEME_FILE" ] && theme_name="$(<"$CURRENT_THEME_FILE")"
theme_file="$THEMES_DIR/$theme_name.theme"
[ -f "$theme_file" ] && source "$theme_file"
SYN_BG="${SYN_BG:-#000000}"
SYN_BG_ALT="${SYN_BG_ALT:-#100000}"
SYN_TEXT="${SYN_TEXT:-#ffffff}"
SYN_ACCENT="${SYN_ACCENT:-#800000}"
SYN_BORDER="${SYN_BORDER:-#444444}"

# rofi's default theme is built on root-level variables (background,
# foreground, lightbg, ...), not per-widget properties — see
# power-menu.zsh, same override set, same reason: overriding only
# window/element-selected leaves the entry box and unselected rows on
# rofi's stock light Solarized palette regardless of the active theme.
rofi_themed() {
  rofi -dmenu -theme-str "* { background: ${SYN_BG}e6; background-color: ${SYN_BG}e6; foreground: $SYN_TEXT; lightbg: $SYN_BG_ALT; lightfg: $SYN_TEXT; selected-normal-background: $SYN_ACCENT; selected-normal-foreground: $SYN_TEXT; border-color: $SYN_BORDER; } window { border: 1px; }" "$@"
}

DEFAULT_DIR="$HOME/Videos"
CHOSEN_DIR=$(printf '%s\n' \
  "$HOME/Videos" \
  "$HOME" \
  "$HOME/Desktop" \
  "$HOME/Downloads" \
  | rofi_themed -p "Save recording to:")
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
  # Build "port-name<TAB>human label" per connected output, e.g.
  #   DP-2<TAB>LG Electronics LG ULTRAWIDE — 3440x1440
  # so the picker can show people a screen they recognize instead of a
  # port name, while still handing wf-recorder the name it needs. Only
  # enabled outputs are listed — a disconnected/sleeping port (e.g. a
  # laptop lid closed) would otherwise offer a choice that can't record.
  # zsh has no mapfile builtin — "${(@f)...}" splits command output into
  # an array by lines instead (bash's mapfile -t equivalent). Unlike
  # mapfile, it turns fully-empty output into a one-element array holding
  # an empty string rather than a zero-length array, hence the explicit
  # guard below.
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
  choices=()
  [[ -n "$randr_out" ]] && choices=("${(@f)randr_out}")
  if (( ${#choices[@]} > 1 )); then
    chosen="$(printf '%s\n' "${choices[@]}" | \
      rofi_themed -p "Record which screen:" -display-columns 2 -display-column-separator '\t' | \
      cut -f1)"
    [[ -z "$chosen" ]] && exit 0
    geometry_args=(-o "$chosen")
  fi
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
