#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                          S Y N - B A R - S P E A K
#
#   The bar's read-aloud button. A Piper voice from the syn-speak store
#   (~/.local/share/syn-speak/voices) reads whatever is highlighted on
#   screen, or the clipboard when nothing is highlighted.
#
#     click         read it (a click while it's reading stops it)
#     right-click   a rofi menu: read the highlight, read the clipboard,
#                   stop, pick the pace, pick the voice (a voice bank
#                   then asks which of its speakers), get more voices
#     middle-click  stop
#
#   SYN-OS ships no voices. "Get more voices" lists the English Piper
#   voices k2-fsa publishes with sherpa-onnx (the tts-models release),
#   downloads the one picked into the store and writes its VOICE
#   manifest, so syn-speak, SYN-CASTER and SYN-TREK all read it the same
#   way. A machine's own voices (a person's, a character's) are installed
#   by SYN-CASTER's voice-install and are never fetched from anywhere.
#
#   The voice, speaker and pace live in ~/.config/syn-os/speak.conf, the
#   way syn-hum keeps hum.conf. Sound is made by syn-speak (Piper on
#   sherpa-onnx, SYN-SOFTWARE/syn-speak-src, docs/SYN-SPEAK.md in
#   SYN-CASTER): one line in on stdin, framed 16 kHz PCM out, unframed by
#   syn-speak-play and played through PipeWire. The speaking pipeline
#   runs in its own process group, so Stop kills all of it. waybar is
#   told with SIGRTMIN+9 whenever the button's state changes.
#
#   Commands: status [--json] | say [primary|clipboard] | stop | menu |
#             fetch | set voice ID [SPEAKER] | set pace N | voices |
#             catalog [--refresh] | install ASSET
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-BAR-SPEAK (Waybar)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
emulate -L zsh
setopt NO_UNSET PIPE_FAIL EXTENDED_GLOB
SELF=${0:A}                   # $0 is the function name inside functions

RUN="${XDG_RUNTIME_DIR:-/tmp}"
CONF="${XDG_CONFIG_HOME:-$HOME/.config}/syn-os/speak.conf"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/syn-os"
CATALOG="$CACHE/speak-catalog.tsv"
PIDFILE="$RUN/syn-bar-speak.pid"
TEXTFILE="$RUN/syn-bar-speak.text"
LOG="$RUN/syn-bar-speak.log"
NOTIFY=/usr/lib/syn-os/syn-bar-notify
PLAY=/usr/lib/syn-os/syn-speak-play
SIGNAL=9                      # waybar's custom/speak has "signal": 9
PACES=(slower 1.15 normal 1.0 faster 0.85)
UPSTREAM=https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models
UPSTREAM_API=https://api.github.com/repos/k2-fsa/sherpa-onnx/releases
CATALOG_DAYS=7                # how long the upstream list is trusted before it's asked again

export SYN_SPEAK_VOICES="${SYN_SPEAK_VOICES:-${XDG_DATA_HOME:-$HOME/.local/share}/syn-speak/voices}"

# The syn-speak program: SYN_SPEAK_BIN, the package's copy, or
# SYN-CASTER's own build on a developer machine.
speak_bin() {
  local c
  for c in "${SYN_SPEAK_BIN:-}" /usr/lib/syn-os/syn-speak /usr/bin/syn-speak \
           "$HOME/GithubProjects/SYN-CASTER/bin/syn-speak"; do
    [[ -n $c && -x $c ]] && { print -r -- "$c"; return 0; }
  done
  return 1
}

# Unframes syn-speak's answer: syn-speak-play from the package, or the
# same loop in Python where only SYN-CASTER's build is around. Exit 3
# when the line made no sound at all (an unknown or refused voice: the
# reason is on syn-speak's stderr).
UNFRAME_PY='
import struct, sys
i, o, total = sys.stdin.buffer, sys.stdout.buffer, 0
while True:
    h = i.read(4)
    if len(h) < 4:
        break
    n = struct.unpack("<I", h)[0]
    if n == 0:
        break
    o.write(i.read(n)); o.flush(); total += n
sys.exit(0 if total else 3)
'
unframe() {
  if [[ -x $PLAY ]]; then "$PLAY"; else python3 -c "$UNFRAME_PY"; fi
}

tone() { [[ -x $NOTIFY ]] && "$NOTIFY" "$1" 2>/dev/null; return 0; }
toast() { command -v notify-send >/dev/null && notify-send -a SYN-BAR "$1" "${2:-}"; return 0; }
refresh() { pkill -RTMIN+$SIGNAL -x waybar 2>/dev/null; return 0; }
die() { print -u2 -r -- "syn-bar-speak: $*"; exit 1; }

# --- what's in use ------------------------------------------------------------
voice=data speaker=0 length=1.0
load_conf() {
  [[ -r $CONF ]] || return 0
  local key value _
  while read -r key value _; do
    case $key in
      voice)   voice=$value ;;
      speaker) speaker=$value ;;
      length)  length=$value ;;
    esac
  done < "$CONF"
}
save_conf() {
  mkdir -p "${CONF:h}"
  printf 'voice %s\nspeaker %s\nlength %s\n' "$voice" "$speaker" "$length" > "$CONF.tmp" && mv "$CONF.tmp" "$CONF"
}

# --- the voice store, as syn-speak --list sees it -----------------------------
# V_IDS in order (own voices first, then banks); V_DISPLAY, V_SET,
# V_SPEAKERS by id. Only voices syn-speak says are ok.
typeset -ga V_IDS
typeset -gA V_DISPLAY V_SET V_SPEAKERS
load_voices() {
  local bin id set speakers display subject consent allow characters aliases ok_
  local -a own bank
  V_IDS=()
  bin=$(speak_bin) || return 1
  while IFS=$'\t' read -r id set speakers display subject consent allow characters aliases ok_; do
    [[ $ok_ == ok ]] || continue
    [[ $allow == *desk* ]] || continue     # may this voice play on this desk at all
    V_DISPLAY[$id]=${display:#-}; : ${V_DISPLAY[$id]:=$id}
    V_SET[$id]=$set; V_SPEAKERS[$id]=$speakers
    if [[ $set == bank ]]; then bank+=("$id"); else own+=("$id"); fi
  done < <("$bin" --list 2>/dev/null)
  V_IDS=("${own[@]}" "${bank[@]}")
  (( ${#V_IDS} ))
}
# "Commander Data", or "VCTK (109 British voices) #59" for a bank speaker
label_of() {
  local id=$1 sid=${2:-0}
  local d=${V_DISPLAY[$id]:-$id}
  [[ ${V_SET[$id]:-own} == bank ]] && d="$d #$sid"
  print -r -- "$d"
}

speaking() { [[ -r $PIDFILE ]] && kill -0 -- "-$(<$PIDFILE)" 2>/dev/null; }

# --- the text: what's highlighted, else the clipboard -------------------------
# $1: auto (highlight, then clipboard), primary or clipboard. Whitespace
# collapses to single spaces: syn-speak's wire format is one line, no tabs.
grab_text() {
  local want=${1:-auto} t=""
  [[ $want != clipboard ]] && t=$(wl-paste --primary --no-newline -t text 2>/dev/null)
  [[ -z ${t//[[:space:]]/} && $want != primary ]] && t=$(wl-paste --no-newline -t text 2>/dev/null)
  print -r -- "${(j: :)${=t}}"
}

# --- speaking -----------------------------------------------------------------
# say_text TEXT: speak it in the voice in use, in a process group of its own
say_text() {
  print -r -- "$1" > "$TEXTFILE"
  setsid -f "$SELF" _play
}

cmd_say() {       # say [auto|primary|clipboard]: a click while reading stops instead
  local want=${1:-auto}
  if speaking; then
    [[ $want == auto ]] && { cmd_stop; return 0; }
    cmd_stop quiet
  fi
  local text; text=$(grab_text "$want")
  if [[ -z $text ]]; then
    tone FAIL
    toast "Nothing to read" "Highlight some text, or copy it, then click again."
    return 1
  fi
  if ! speak_bin >/dev/null; then
    tone FAIL
    toast "No voice" "syn-speak isn't installed, so nothing can read aloud."
    return 1
  fi
  if ! load_voices; then
    tone FAIL
    toast "No voices yet" "Right-click the button and pick Get more voices."
    return 1
  fi
  say_text "$text"
}

cmd__play() {     # the speaking pipeline, in its own process group (setsid)
  print -r -- $$ > "$PIDFILE"
  trap 'rm -f "$PIDFILE"; refresh' EXIT
  refresh
  local bin text player
  bin=$(speak_bin) || exit 1
  text=$(<"$TEXTFILE")
  if command -v pw-play >/dev/null; then
    player=(pw-play --rate 16000 --channels 1 --format s16 --raw -)
  else
    player=(paplay --raw --rate=16000 --channels=1 --format=s16le)
  fi
  print -r -- "$(date '+%F %T') $voice#$speaker x$length: ${text[1,80]}" >> "$LOG"
  printf '%s\t%s\t%s\t%s\n' "$voice" "$speaker" "$text" "$length" \
    | "$bin" --serve 2>> "$LOG" \
    | unframe \
    | "${player[@]}" 2>> "$LOG"
  local -a st=("${pipestatus[@]}")
  if (( st[3] == 3 )); then
    tone FAIL
    toast "The voice couldn't read that" "$(tail -n 1 "$LOG")"
  fi
}

cmd_stop() {      # stop [quiet]
  if [[ -r $PIDFILE ]]; then
    kill -TERM -- "-$(<$PIDFILE)" 2>/dev/null
    rm -f "$PIDFILE"
    [[ ${1:-} == quiet ]] || tone STOP
  fi
  refresh
}

cmd_status() {    # status [--json]: the button's state, alt and tooltip for waybar
  local state tip label
  load_voices
  label=$(label_of "$voice" "$speaker")
  if ! speak_bin >/dev/null; then
    state=none
    tip="Read aloud: syn-speak isn't installed"
  elif (( ! ${#V_IDS} )); then
    state=none
    tip="Read aloud: no voices yet"$'\n'"Right-click: get a voice"
  elif speaking; then
    state=speaking
    tip="Reading aloud as $label"$'\n'"Click to stop"
  else
    state=idle
    tip="Read aloud as $label"$'\n'"Click: read what's highlighted, or the clipboard"$'\n'"Right-click: voices and pace"$'\n'"Middle-click: stop"
  fi
  if [[ ${1:-} == --json ]]; then
    tip=${tip//\\/\\\\}; tip=${tip//\"/\\\"}; tip=${tip//$'\n'/\\n}
    printf '{"text":"","alt":"%s","class":"%s","tooltip":"%s"}\n' "$state" "$state" "$tip"
  else
    print -r -- "$state: $label (pace x$length)"
    print -r -- "$tip"
  fi
}

cmd_set() {       # set voice ID [SPEAKER] | set pace N
  case ${1:-} in
    voice)
      [[ -n ${2:-} ]] || { print -u2 "usage: $SELF set voice ID [SPEAKER]"; return 2; }
      voice=$2 speaker=${3:-0}
      ;;
    pace)
      [[ ${2:-} == <->(.<->)# ]] || { print -u2 "usage: $SELF set pace N   (0.5 to 2.0, 1.0 normal)"; return 2; }
      length=$2
      ;;
    *) print -u2 "usage: $SELF set voice ID [SPEAKER] | set pace N"; return 2 ;;
  esac
  save_conf
  refresh
}

cmd_voices() {    # the voices this desk may read with
  load_voices || { print -u2 "no voices"; return 1; }
  local id
  for id in "${V_IDS[@]}"; do
    printf '%-24s %-5s %4s  %s\n' "$id" "${V_SET[$id]}" "${V_SPEAKERS[$id]}" "${V_DISPLAY[$id]}"
  done
}

# --- the right-click menu -----------------------------------------------------
# One flat rofi list (type to filter), the way syn-bar-share-quickmenu
# does it; what's in use is tagged [CURRENT] like syn-pipe-hum's entries.
cmd_menu() {
  source /usr/lib/syn-os/syn-theme-lib.zsh
  source /usr/lib/syn-os/syn-picker-lib.zsh
  syn_theme_load
  load_voices
  local -a lines
  local id line name p
  speaking && lines+=("Stop reading")
  if (( ${#V_IDS} )); then
    lines+=("Read the highlighted text" "Read the clipboard")
    for name p in "${PACES[@]}"; do
      line="Pace: $name"
      [[ $p == $length ]] && line="$line [CURRENT]"
      lines+=("$line")
    done
  fi
  lines+=("Get more voices...")
  for id in "${V_IDS[@]}"; do
    line="${V_DISPLAY[$id]}  ($id)"
    if [[ $id == $voice ]]; then
      if [[ ${V_SET[$id]} == bank ]]; then line="$line [CURRENT #$speaker]"; else line="$line [CURRENT]"; fi
    fi
    lines+=("$line")
  done

  local choice
  choice=$(printf '%s\n' "${lines[@]}" | syn_pick::rofi "READ ALOUD:" -i)
  [[ -z $choice ]] && return 0

  case $choice in
    "Stop reading")              cmd_stop ;;
    "Read the highlighted text") cmd_say primary ;;
    "Read the clipboard")        cmd_say clipboard ;;
    "Get more voices...")        cmd_fetch ;;
    "Pace: "*)
      name=${${choice#Pace: }%% *}
      for id p in "${PACES[@]}"; do [[ $id == $name ]] && length=$p; done
      save_conf; refresh
      ;;
    *)
      choice=${choice%% \[CURRENT*}
      id=${choice##* \(}; id=${id%\)}
      [[ -n ${V_DISPLAY[$id]:-} ]] || return 0
      local sid=0
      if [[ ${V_SET[$id]} == bank ]]; then
        sid=$(pick_speaker "$id") || return 0
      fi
      voice=$id speaker=$sid
      save_conf; refresh
      # hear it at once, the way syn-hum's toggle answers with a tone
      cmd_stop quiet
      say_text "${V_DISPLAY[$id]}. Reading aloud."
      ;;
  esac
}

# A voice bank's census (speakers.txt: "<sid> <m|f> <f0 Hz>", each sex by
# pitch; "?" where a fetched bank hasn't been measured) as a second
# picker; prints the chosen speaker id.
pick_speaker() {
  local id=$1 census="$SYN_SPEAK_VOICES/$id/speakers.txt" sid sex f0 line _
  local -a lines
  if [[ -r $census ]]; then
    while read -r sid sex f0 _; do
      [[ $sid == <-> ]] || continue
      line="#$sid  ${sex:-?}  ${f0:-?} Hz"
      [[ $id == $voice && $sid == $speaker ]] && line="$line [CURRENT]"
      lines+=("$line")
    done < "$census"
  else
    local n=${V_SPEAKERS[$id]:-1} i
    for (( i = 0; i < n; i++ )); do lines+=("#$i"); done
  fi
  local choice
  choice=$(printf '%s\n' "${lines[@]}" | syn_pick::rofi "${V_DISPLAY[$id]} SPEAKER:" -i)
  [[ -n $choice ]] || return 1
  sid=${${choice#\#}%% *}
  [[ $sid == <-> ]] || return 1
  print -r -- "$sid"
}

# --- getting voices -----------------------------------------------------------
# The upstream list: every English Piper voice in k2-fsa's sherpa-onnx
# tts-models release, full precision only (the int8 and fp16 cuts are
# the same voices smaller and worse). Kept CATALOG_DAYS in the cache as
# "<asset>\t<MB>" lines; the API is asked again after that, or with
# --refresh, and the cached list is kept when it can't be reached.
catalog_fresh() {
  [[ -s $CATALOG ]] || return 1
  local now=$(date +%s) then=$(stat -c %Y "$CATALOG" 2>/dev/null || print 0)
  (( now - then < CATALOG_DAYS * 86400 ))
}
catalog_refresh() {
  local rel id page json tmp="$CATALOG.tmp" n=0
  rel=$(curl -fsSL -H 'Accept: application/vnd.github+json' "$UPSTREAM_API/tags/tts-models") || return 1
  id=$(print -r -- "$rel" | grep -o -m1 '"id": *[0-9]*' | tr -dc '0-9')
  [[ -n $id ]] || return 1
  mkdir -p "$CACHE"
  : > "$tmp"
  for page in {1..10}; do
    json=$(curl -fsSL -H 'Accept: application/vnd.github+json' "$UPSTREAM_API/$id/assets?per_page=100&page=$page") || break
    # each asset's "name" comes before its "size"; pair them up
    print -r -- "$json" | grep -o '"name": *"[^"]*"\|"size": *[0-9]*' \
      | awk -F'"' '/"name"/ { name=$4; next } /"size"/ { split($0, a, ":"); gsub(/ /, "", a[2]); if (name != "") printf "%s\t%d\n", name, (a[2] + 524288) / 1048576; name="" }' \
      | grep -P '^vits-piper-en_[^\t]*\.tar\.bz2\t' | grep -v -- '-int8\.\|-fp16\.' >> "$tmp"
    (( $(print -r -- "$json" | grep -c '"name"') < 100 )) && break
  done
  n=$(grep -c . "$tmp")
  (( n )) || { rm -f "$tmp"; return 1; }
  sed -i 's/\.tar\.bz2\t/\t/' "$tmp"
  sort -o "$tmp" "$tmp" && mv "$tmp" "$CATALOG"
}
# asset_parts ASSET: sets lang, name, quality, id, display from
# "vits-piper-en_GB-alba-medium"; the id is the name (lessac), plus the
# quality when it isn't medium (lessac_high, amy_low), the way the
# store already names them.
asset_parts() {
  local rest=${1#vits-piper-}
  lang=${rest[1,5]}; rest=${rest[7,-1]}
  quality=""
  case $rest in
    *-low)    quality=low;    rest=${rest%-low} ;;
    *-medium) quality=medium; rest=${rest%-medium} ;;
    *-high)   quality=high;   rest=${rest%-high} ;;
  esac
  name=$rest
  id=${${name//-/_}:l}
  [[ -n $quality && $quality != medium ]] && id="${id}_${quality}"
  local where
  case $lang in
    en_GB) where=British ;;
    en_US) where=American ;;
    *) where=$lang ;;
  esac
  local -a words=("${(s:_:)${name//-/_}}")
  display="${(C)words[*]}"
  # the names that aren't words
  case $name in
    glados)     display=GLaDOS ;;
    vctk)       display=VCTK ;;
    aru)        display=ARU ;;
    dii)        display=DII ;;
    hfc_*)      display="HFC ${(C)name#hfc_}" ;;
    ljspeech)   display=LJSpeech ;;
    l2arctic)   display=L2-ARCTIC ;;
    libritts)   display=LibriTTS ;;
    libritts_r) display=LibriTTS-R ;;
  esac
  display="$display ($where${quality:+, $quality})"
}
cmd_catalog() {   # catalog [--refresh]: the upstream voices, "<asset> <MB> <id> <display> [installed]"
  if [[ ${1:-} == --refresh ]] || ! catalog_fresh; then
    catalog_refresh || [[ -s $CATALOG ]] || die "can't reach $UPSTREAM_API and nothing is cached"
  fi
  local asset mb lang name quality id display
  while IFS=$'\t' read -r asset mb; do
    asset_parts "$asset"
    printf '%s\t%s\t%s\t%s\t%s\n' "$asset" "$mb" "$id" "$display" "$([[ -d $SYN_SPEAK_VOICES/$id ]] && print installed)"
  done < "$CATALOG"
}

cmd_fetch() {     # the menu's "Get more voices": pick one, then install it in a popup
  source /usr/lib/syn-os/syn-theme-lib.zsh
  source /usr/lib/syn-os/syn-picker-lib.zsh
  source /usr/lib/syn-os/syn-popup-lib.zsh
  syn_theme_load
  local -a lines
  local asset mb id display installed
  while IFS=$'\t' read -r asset mb id display installed; do
    [[ -n $installed ]] && continue
    lines+=("$display  ${mb} MB  [$asset]")
  done < <(cmd_catalog 2>/dev/null)
  if (( ! ${#lines} )); then
    tone FAIL
    toast "No voices to get" "The upstream list couldn't be fetched, or every voice on it is already here."
    return 1
  fi
  local choice
  choice=$(printf '%s\n' "${lines[@]}" | syn_pick::rofi "GET A VOICE:" -i)
  [[ -n $choice ]] || return 0
  asset=${${choice##*\[}%\]}
  [[ $asset == vits-piper-* ]] || return 0
  syn_popup::run "$SELF" install "$asset"
}

cmd_install() {   # install ASSET: download it from upstream, write VOICE, make it the voice in use
  local asset=${1:-}
  [[ $asset == vits-piper-* ]] || die "usage: $SELF install vits-piper-<lang>-<name>-<quality>"
  local lang name quality id display
  asset_parts "$asset"
  [[ -n $id ]] || die "no voice id in $asset"
  [[ -d $SYN_SPEAK_VOICES/$id ]] && die "$id is already in the store ($SYN_SPEAK_VOICES/$id)"
  local bin; bin=$(speak_bin) || die "syn-speak isn't installed"
  mkdir -p "$SYN_SPEAK_VOICES" "$CACHE"
  # the EXIT trap runs after this function's locals are gone, so the
  # download folder is a global
  typeset -g FETCH_TMP
  FETCH_TMP=$(mktemp -d "$CACHE/fetch.XXXXXX") || die "no room in $CACHE"
  local tmp=$FETCH_TMP
  trap 'rm -rf "${FETCH_TMP:?}"' EXIT
  print -r -- "Getting $display from k2-fsa's sherpa-onnx voices..."
  curl -fL --progress-bar -o "$tmp/$asset.tar.bz2" "$UPSTREAM/$asset.tar.bz2" || die "download failed"
  tar xjf "$tmp/$asset.tar.bz2" -C "$tmp" || die "the archive wouldn't unpack"
  local dir="$tmp/$asset"
  [[ -d $dir ]] || dir=$(print -l "$tmp"/*(/N) | head -n 1)
  local -a onnx=("$dir"/*.onnx(N))
  (( ${#onnx} == 1 )) || die "expected one .onnx in the archive, found ${#onnx}"
  local onnx_name=${onnx[1]:t} json="$dir/${onnx[1]:t}.json"
  [[ -r $json && -r $dir/tokens.txt && -d $dir/espeak-ng-data ]] || die "the archive is missing its config, tokens or espeak data"
  local speakers rate espeak licence set tier
  speakers=$(grep -o '"num_speakers": *[0-9]*' "$json" | tr -dc '0-9'); : ${speakers:=1}
  rate=$(grep -o '"sample_rate": *[0-9]*' "$json" | tr -dc '0-9'); : ${rate:=22050}
  espeak=$(grep -o '"voice": *"[^"]*"' "$json" | head -n 1 | cut -d'"' -f4); : ${espeak:=en-us}
  licence=$(grep -m1 -i 'license' "$dir/MODEL_CARD" 2>/dev/null | sed 's/^\* *[Ll]icense: *//'); : ${licence:=see MODEL_CARD}
  if (( speakers > 1 )); then set=bank; else set=own; fi
  [[ $quality == low ]] && tier=LIGHT || tier=FINE
  local redistribute="see MODEL_CARD"
  [[ ${licence:l} == *(cc-|creative|public domain|unlicense|mit|apache|bsd)* ]] && redistribute=yes
  {
    print -r -- '# What syn-speak, SYN-CASTER and SYN-TREK read about this voice (docs/SYN-SPEAK.md has the format). One "key value" a line.'
    print -r -- "format 1"
    print -r -- "id $id"
    print -r -- "set $set"
    print -r -- "engine piper"
    print -r -- "display $display"
    print -r -- "onnx $onnx_name"
    print -r -- "config $onnx_name.json"
    print -r -- "tokens tokens.txt"
    print -r -- "espeak $espeak"
    print -r -- "espeak_data espeak-ng-data"
    print -r -- "rate $rate"
    print -r -- "quality ${quality:-medium}"
    print -r -- "speakers $speakers"
    print -r -- "inference 0.667 0.8 1.0"
    if [[ $set == bank ]]; then
      print -r -- "tier $tier"
      print -r -- "census speakers.txt"
      print -r -- "allow channel desk trek"
    else
      print -r -- "subject character"
      print -r -- "characters -"
      print -r -- "consent n/a"
      print -r -- "allow channel desk"
    fi
    print -r -- "licence $licence"
    print -r -- "redistribute $redistribute"
    print -r -- "provenance upstream"
    print -r -- "source_detail k2-fsa sherpa-onnx tts-models $asset, fetched $(date +%F) by syn-bar-speak"
  } > "$dir/VOICE"
  if [[ $set == bank && ! -r $dir/speakers.txt ]]; then
    # a census with the numbers only: sex and pitch aren't measured here
    { print -r -- "# <sid> <m|f> <f0 Hz>; sex and pitch unmeasured (?) for a voice fetched by syn-bar-speak"
      local i; for (( i = 0; i < speakers; i++ )); do print -r -- "$i ? ?"; done; } > "$dir/speakers.txt"
  fi
  mv "$dir" "$SYN_SPEAK_VOICES/$id" || die "couldn't move the voice into the store"
  local row; row=$("$bin" --list 2>&1 | grep -P "^$id\t")
  if [[ $row != *$'\t'ok ]]; then
    print -u2 -r -- "syn-speak refuses $id: ${row##*$'\t'}"
    rm -rf "${SYN_SPEAK_VOICES:?}/${id:?}"
    exit 1
  fi
  print -r -- "$display is in the store as $id."
  voice=$id speaker=0
  save_conf; refresh
  tone SUCCESS
  cmd_stop quiet
  say_text "$display. Reading aloud."
}

load_conf
case ${1:-status} in
  status)  cmd_status "${2:-}" ;;
  say)     cmd_say "${2:-auto}" ;;
  _play)   cmd__play ;;
  stop)    cmd_stop ;;
  menu)    cmd_menu ;;
  fetch)   cmd_fetch ;;
  catalog) cmd_catalog "${2:-}" ;;
  install) cmd_install "${2:-}" ;;
  set)     shift; cmd_set "$@" ;;
  voices)  cmd_voices ;;
  *)
    print -u2 "usage: $SELF status [--json] | say [primary|clipboard] | stop | menu | fetch | catalog [--refresh] | install ASSET | set voice ID [SPEAKER] | set pace N | voices"
    exit 2
    ;;
esac
