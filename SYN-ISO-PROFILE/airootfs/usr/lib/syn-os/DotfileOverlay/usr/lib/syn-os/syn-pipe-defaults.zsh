#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                      S Y N - P I P E - D E F A U L T S
#
#   Labwc pipe menu for default apps: what xdg-open (and so SYN-SHELL,
#   the browser, Steam, the bar) opens each kind of file and link with.
#   By category, not by MIME type: Audio, Images, Archives... Each lists
#   the installed apps that say they can open it, the one in charge now
#   tagged [DEFAULT] the same way syn-pipe-audio tags its devices.
#
#   Choosing one sets every type in the category that app opens: the ones
#   it declares, the ones descending from those (a C file is text/plain to
#   shared-mime-info, so a text editor gets it too, though it only says
#   text/plain), and every alias of each: a WAV is audio/vnd.wave by its
#   extension and audio/x-wav to `file`, which xdg-open uses here, so
#   setting only one of the names was how a WAV opened in the wrong app.
#
#   Choices go to ~/.config/mimeapps.list. SYN-OS's own defaults sit
#   under it in /etc/xdg/mimeapps.list, never written here; "Reset to
#   SYN-OS default" just drops a category's lines from the user's file.
#
#   The [DEFAULT] tags follow xdg-mime's own lookup (desktop-specific
#   lists, then mimeapps.list, config dirs before data dirs, then
#   mimeinfo.cache), done here in one pass so the menu opens quickly.
#
#   Usage: syn-pipe-defaults.zsh                  (the menu, for labwc)
#          syn-pipe-defaults.zsh --set CAT APP    (APP as foo.desktop)
#          syn-pipe-defaults.zsh --reset CAT
#          syn-pipe-defaults.zsh --types CAT APP  (the types --set writes)
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-DEFAULTS (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -uo pipefail
setopt extendedglob

SELF="${0:A}"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
USER_LIST="$CONFIG_HOME/mimeapps.list"
typeset -a CONFIG_DIRS DATA_DIRS DESKTOPS
CONFIG_DIRS=("$CONFIG_HOME" ${(s.:.)${XDG_CONFIG_DIRS:-/etc/xdg}})
DATA_DIRS=("${XDG_DATA_HOME:-$HOME/.local/share}" ${(s.:.)${XDG_DATA_DIRS:-/usr/local/share:/usr/share}})
DATA_DIRS=(${DATA_DIRS%/})
DESKTOPS=(${(s.:.)${(L)XDG_CURRENT_DESKTOP:-}})

# ---- categories ----------------------------------------------------------
# Patterns are matched against every type some installed app declares.
typeset -a CAT_ORDER
CAT_ORDER=(web files archives text images audio video documents torrents)
typeset -A CAT_LABEL CAT_TYPES CAT_EXCLUDE
CAT_LABEL=(
  web       "Web browser"
  files     "Folders"
  archives  "Archives"
  text      "Text and code"
  images    "Images"
  audio     "Audio"
  video     "Video"
  documents "PDF and e-books"
  torrents  "Torrents"
)
CAT_TYPES=(
  web       "x-scheme-handler/http x-scheme-handler/https text/html application/xhtml+xml"
  files     "inode/directory"
  archives  "application/zip application/x-tar application/x-*compressed-tar application/x-tarz
             application/x-7z-compressed application/vnd.rar application/x-rar*
             application/gzip application/x-gzip application/x-bzip* application/x-xz application/zstd
             application/x-lz4 application/x-lzip application/x-lzma application/x-compress
             application/x-cpio application/x-archive application/vnd.ms-cab-compressed
             application/x-lha application/x-lzh-compressed application/x-xar"
  text      "text/* application/x-shellscript application/json application/xml application/toml
             application/yaml application/x-yaml application/x-desktop"
  images    "image/*"
  audio     "audio/*"
  video     "video/*"
  documents "application/pdf application/epub+zip application/vnd.comicbook*"
  torrents  "application/x-bittorrent x-scheme-handler/magnet"
)
# What makes an app one of a category's apps: it opens this type. A
# text editor opens text/plain, a player audio/mpeg; VLC claiming a
# subtitle format doesn't make it a text editor.
typeset -A CAT_KEY
CAT_KEY=(
  web       x-scheme-handler/https
  files     inode/directory
  archives  application/zip
  text      text/plain
  images    image/png
  audio     audio/mpeg
  video     video/mp4
  documents application/pdf
  torrents  application/x-bittorrent
)
# Categories where opening the key type means opening all of them: every
# type under Text is text, whatever shared-mime-info says it descends
# from (shell scripts descend from "executable", not text/plain).
typeset -A CAT_WHOLE
CAT_WHOLE=(text 1)
# text/* would otherwise take the browser's and the calendar's types.
CAT_EXCLUDE=(
  text "text/html text/calendar text/vcard text/x-vcard"
)

# ---- what's installed ----------------------------------------------------
# HANDLERS[type] = "a.desktop;b.desktop;" from every mimeinfo.cache, the
# user's first, as xdg-mime's own fallback reads them.
typeset -A HANDLERS
for d in $DATA_DIRS; do
  cache="$d/applications/mimeinfo.cache"
  [[ -r $cache ]] || continue
  while IFS='=' read -r type apps; do
    [[ $type == \[* || -z $apps ]] && continue
    HANDLERS[$type]="${HANDLERS[$type]:-}$apps"
  done < "$cache"
done

typeset -A DESKTOP_FILE APP_NAME APP_HIDDEN
desktop_file() {
  local id=$1 d
  if (( ! ${+DESKTOP_FILE[$id]} )); then
    DESKTOP_FILE[$id]=""
    for d in $DATA_DIRS; do
      [[ -f $d/applications/$id ]] && { DESKTOP_FILE[$id]=$d/applications/$id; break; }
    done
  fi
  REPLY=${DESKTOP_FILE[$id]}
}
# Name= and whether it's NoDisplay/Hidden, read once per app.
app_name() {
  local id=$1 line in_entry=0 named=0
  if (( ! ${+APP_NAME[$id]} )); then
    APP_NAME[$id]=${id%.desktop}
    APP_HIDDEN[$id]=0
    desktop_file $id
    if [[ -n $REPLY ]]; then
      while IFS= read -r line; do
        [[ $line == '[Desktop Entry]' ]] && { in_entry=1; continue; }
        [[ $line == \[* ]] && in_entry=0
        (( in_entry )) || continue
        if [[ $line == Name=* ]] && (( ! named )); then
          APP_NAME[$id]=${line#Name=}
          named=1
        fi
        [[ $line == (NoDisplay|Hidden)=true ]] && APP_HIDDEN[$id]=1
      done < $REPLY
    else
      APP_HIDDEN[$id]=1
    fi
  fi
  REPLY=${APP_NAME[$id]}
}
# True when APP declares TYPE under any of its names.
declares() {
  local app=$1 t
  with_aliases $2
  for t in $reply; do
    [[ ";${HANDLERS[$t]:-}" == *";$app;"* ]] && return 0
  done
  return 1
}
# True when APP declares TYPE or a type it descends from.
opens() {
  local app=$1 type=$2 p
  local -i depth=${3:-0}
  (( depth > 6 )) && return 1
  declares $app $type && return 0
  for p in ${=PARENTS[${CANON[$type]:-$type}]:-}; do
    opens $app $p $(( depth + 1 )) && return 0
  done
  return 1
}

# ---- aliases -------------------------------------------------------------
typeset -A CANON ALIASES
if [[ -r /usr/share/mime/aliases ]]; then
  while read -r alias canonical; do
    CANON[$alias]=$canonical
    ALIASES[$canonical]="${ALIASES[$canonical]:-} $alias"
  done < /usr/share/mime/aliases
fi
# reply = the given types and all their other names.
with_aliases() {
  local t c
  local -a out
  for t in "$@"; do
    c=${CANON[$t]:-$t}
    out+=($c ${=ALIASES[$c]:-})
  done
  reply=(${(u)out})
}

# Parents, from shared-mime-info: text/x-csrc is a text/plain.
typeset -A PARENTS
if [[ -r /usr/share/mime/subclasses ]]; then
  while read -r child parent; do
    PARENTS[$child]="${PARENTS[$child]:-} $parent"
  done < /usr/share/mime/subclasses
fi
# Every type shared-mime-info knows, whether or not an app claims it.
typeset -a KNOWN_TYPES
[[ -r /usr/share/mime/types ]] && KNOWN_TYPES=(${(f)"$(</usr/share/mime/types)"})

# ---- what xdg-open would pick now ----------------------------------------
# EFFECTIVE[type] = app. Lists in precedence order; the first list that
# names an installed app for a type decides it.
typeset -A EFFECTIVE
typeset -a LISTS
for d in $CONFIG_DIRS; do
  for p in $DESKTOPS ''; do LISTS+=("$d/${p:+$p-}mimeapps.list"); done
done
for d in $DATA_DIRS; do
  for p in $DESKTOPS ''; do LISTS+=("$d/applications/${p:+$p-}mimeapps.list"); done
done
for list in $LISTS; do
  [[ -r $list ]] || continue
  in_default=0
  while IFS= read -r line; do
    if [[ $line == \[* ]]; then
      [[ $line == '[Default Applications]'* ]] && in_default=1 || in_default=0
      continue
    fi
    (( in_default )) && [[ $line == *=* ]] || continue
    type=${line%%=*}
    (( ${+EFFECTIVE[$type]} )) && continue
    for app in ${(s.;.)${line#*=}}; do
      desktop_file $app
      [[ -n $REPLY ]] && { EFFECTIVE[$type]=$app; break; }
    done
  done < $list
done
effective() {
  local t=$1 app
  REPLY=${EFFECTIVE[$t]:-}
  [[ -n $REPLY ]] && return
  for app in ${(s.;.)${HANDLERS[$t]:-}}; do
    desktop_file $app
    [[ -n $REPLY ]] && { REPLY=$app; return; }
  done
  REPLY=""
}

# ---- categories against what's installed -------------------------------
# The types in a category: every type matching its patterns, from
# shared-mime-info's list and from what apps declare (x-scheme-handler/*
# lives only there), plus the patterns that are plain type names.
category_types() {
  local cat=$1 t pat
  typeset -A seen
  local -a pats excl
  pats=(${=CAT_TYPES[$cat]})
  excl=(${=CAT_EXCLUDE[$cat]:-})
  for t in ${(u)KNOWN_TYPES} ${(k)HANDLERS}; do
    (( ${excl[(Ie)$t]} )) && continue
    for pat in $pats; do
      [[ $t == ${~pat} ]] && { seen[$t]=1; break; }
    done
  done
  for pat in $pats; do
    [[ $pat == *'*'* ]] || seen[$pat]=1
  done
  reply=(${(k)seen})
}

# The types --set writes for an app: what it opens in the category,
# with every alias of each.
types_for() {
  local cat=$1 app=$2 t
  local -a mine all
  category_types $cat
  all=($reply)
  if (( ${+CAT_WHOLE[$cat]} )) && opens $app ${CAT_KEY[$cat]}; then
    mine=($all)
  else
    for t in $all; do
      opens $app $t && mine+=($t)
    done
  fi
  with_aliases $mine
}

# ---- writing ~/.config/mimeapps.list ------------------------------------
# set_entries APP TYPE...: TYPE=APP in [Default Applications], replacing
# what was there. APP "" removes the types instead. Everything else in
# the file is kept as it was; the file is replaced in one rename.
set_entries() {
  local app=$1; shift
  typeset -A want
  local t
  for t in "$@"; do want[$t]=1; done
  local -a out
  local line key in_default=0 seen_section=0 last_key=0
  if [[ -f $USER_LIST ]]; then
    while IFS= read -r line || [[ -n $line ]]; do
      if [[ $line == \[* ]]; then
        if (( in_default )) && [[ -n $app ]] && (( ${#want} )); then
          out[last_key+1,last_key]=("${(@)${(@k)want}/%/=$app;}")
          want=()
        fi
        in_default=0
        if [[ $line == '[Default Applications]'* ]]; then
          in_default=1
          seen_section=1
        fi
        out+=("$line")
        last_key=${#out}
        continue
      fi
      if (( in_default )) && [[ $line == *=* ]]; then
        key=${line%%=*}
        if (( ${+want[$key]} )); then
          [[ -n $app ]] && out+=("$key=$app;")
          unset "want[$key]"
          last_key=${#out}
          continue
        fi
        out+=("$line")
        last_key=${#out}
        continue
      fi
      out+=("$line")
    done < $USER_LIST
  fi
  if [[ -n $app ]] && (( ${#want} )); then
    if (( in_default )); then
      out[last_key+1,last_key]=("${(@)${(@k)want}/%/=$app;}")
    elif (( ! seen_section )); then
      [[ ${#out} -gt 0 && -n ${out[-1]} ]] && out+=("")
      out+=("[Default Applications]" "${(@)${(@k)want}/%/=$app;}")
    fi
  fi
  mkdir -p ${USER_LIST:h}
  print -rl -- "${out[@]}" > "$USER_LIST.syn-tmp" && mv "$USER_LIST.syn-tmp" "$USER_LIST"
}

user_has_entries() {
  local cat=$1 line t in_default=0
  [[ -r $USER_LIST ]] || return 1
  category_types $cat
  with_aliases $reply
  typeset -A types
  for t in $reply; do types[$t]=1; done
  while IFS= read -r line; do
    if [[ $line == \[* ]]; then
      [[ $line == '[Default Applications]'* ]] && in_default=1 || in_default=0
      continue
    fi
    (( in_default )) && [[ $line == *=* ]] && (( ${+types[${line%%=*}]} )) && return 0
  done < $USER_LIST
  return 1
}

# ---- command line --------------------------------------------------------
case "${1:-}" in
  --set)
    cat=${2:-} app=${3:-}
    [[ -n ${CAT_LABEL[$cat]:-} && -n $app ]] || { print -u2 "usage: $0 --set CATEGORY APP.desktop"; exit 2; }
    types_for $cat $app
    (( ${#reply} )) || { print -u2 "$app declares no ${CAT_LABEL[$cat]} types"; exit 1; }
    set_entries $app $reply
    app_name $app
    notify-send "Default apps" "${CAT_LABEL[$cat]} open in $REPLY (${#reply} types)" 2>/dev/null || true
    exit 0
    ;;
  --reset)
    cat=${2:-}
    [[ -n ${CAT_LABEL[$cat]:-} ]] || { print -u2 "usage: $0 --reset CATEGORY"; exit 2; }
    category_types $cat
    with_aliases $reply
    set_entries "" $reply
    notify-send "Default apps" "${CAT_LABEL[$cat]}: back to SYN-OS's default" 2>/dev/null || true
    exit 0
    ;;
  --types)
    types_for ${2:-} ${3:-}
    print -l -- ${(o)reply}
    exit 0
    ;;
esac

# ---- the menu ------------------------------------------------------------
xml_escape() {
  print -r -- "$1" | sed \
    -e 's/&/\&amp;/g' \
    -e 's/"/\&quot;/g' \
    -e "s/'/\&apos;/g" \
    -e 's/</\&lt;/g' \
    -e 's/>/\&gt;/g'
}

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'

typeset -a candidates

for cat in $CAT_ORDER; do
  key=${CAT_KEY[$cat]}
  effective $key
  owner=$REPLY

  # The category's apps: every installed app that opens its key type,
  # one per name. A name twice (google-chrome.desktop and its hidden
  # twin com.google.Chrome.desktop) keeps the one in charge, else the
  # visible one. Hidden alone doesn't drop an app: Arch hides feh.
  typeset -A by_name
  by_name=()
  with_aliases $key
  for t in $reply; do
    for app in ${(s.;.)${HANDLERS[$t]:-}}; do
      desktop_file $app
      [[ -n $REPLY ]] || continue
      app_name $app
      name=$REPLY
      if (( ${+by_name[$name]} )); then
        kept=${by_name[$name]}
        [[ $kept == $owner ]] && continue
        [[ $app == $owner ]] || (( APP_HIDDEN[$kept] && ! APP_HIDDEN[$app] )) || continue
      fi
      by_name[$name]=$app
    done
  done
  candidates=()
  for name app in ${(kv)by_name}; do candidates+=("$name"$'\t'"$app"); done
  (( ${#candidates} )) || [[ -n $owner ]] || continue

  # "mixed": some type the owner can open is set to another app.
  summary="nothing set"
  if [[ -n $owner ]]; then
    app_name $owner
    summary=$REPLY
    if declares $owner $key; then
      category_types $cat
      for t in $reply; do
        [[ ";${HANDLERS[$t]:-}" == *";$owner;"* ]] || continue
        effective $t
        [[ $REPLY == $owner ]] || { summary="$summary, mixed"; break; }
      done
    else
      summary="$summary (which can't open these)"
    fi
  fi

  print "  <menu id=\"defaults-$cat\" label=\"$(xml_escape "${CAT_LABEL[$cat]}: $summary")\">"
  for entry in ${(o)candidates}; do
    name=${entry%%$'\t'*} app=${entry#*$'\t'}
    [[ $app == $owner ]] && name="$name [DEFAULT]"
    print "    <item label=\"$(xml_escape "$name")\">"
    print "      <action name=\"Execute\"><command>$SELF --set $cat $app</command></action>"
    print "    </item>"
  done
  if user_has_entries $cat; then
    print '    <separator/>'
    print '    <item label="Reset to SYN-OS default">'
    print "      <action name=\"Execute\"><command>$SELF --reset $cat</command></action>"
    print '    </item>'
  fi
  print '  </menu>'
done

print '  <separator/>'
print '  <item label="Edit mimeapps.list">'
print "    <action name=\"Execute\"><command>xdg-open $USER_LIST</command></action>"
print '  </item>'
print '</openbox_pipe_menu>'
