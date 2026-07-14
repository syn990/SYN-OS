#!/usr/bin/env zsh
# SYN-GRAPHMAP: recursively graphs a directory tree with Graphviz's dot
# layout, styled in the live SYN-OS theme. Rewrite of the original
# GRAPH.sh (kept for reference at docs/graphviz-src/GRAPH.sh.orig).
#
# Usage: syn-graphmap.zsh [directory]   (prompts via wmenu if omitted)

set -euo pipefail

EXCLUDE_NAMES=(.git .cache node_modules WORKDIR .syncache)
MAX_DEPTH=6

CURRENT_THEME_FILE="$HOME/.config/syn-os/current-theme"
THEMES_DIR="$HOME/.config/syn-os/themes"
theme_name="SYN-OS-RED"
[[ -f "$CURRENT_THEME_FILE" ]] && theme_name="$(<"$CURRENT_THEME_FILE")"
theme_file="$THEMES_DIR/$theme_name.theme"
[[ -f "$theme_file" ]] && source "$theme_file"
SYN_BG="${SYN_BG:-#000000}"
SYN_TEXT="${SYN_TEXT:-#ffffff}"
SYN_ACCENT="${SYN_ACCENT:-#800000}"
SYN_ACCENT_DIM="${SYN_ACCENT_DIM:-#260101}"

target="${1:-}"
if [[ -z "$target" ]]; then
  print "SYN-GRAPHMAP: a popup will ask which directory to graph — enter or pick one, then check back here."
  target="$(print -l -- "$PWD" "$HOME" "$HOME/GithubProjects" \
    | wmenu -N "$SYN_BG" -n "$SYN_TEXT" -S "$SYN_ACCENT" -s "$SYN_BG" -M "$SYN_ACCENT_DIM" -m "$SYN_TEXT" -p "Directory to graph:")"
fi
[[ -n "$target" ]] || { print -u2 "No directory given."; exit 1; }
[[ -d "$target" ]] || { print -u2 "Not a directory: $target"; exit 1; }
target="${target:A}"
print "Scanning ${target}…"

outDir="$HOME/Pictures/SYN-GRAPHMAP"
mkdir -p "$outDir"
dotFile="$outDir/$(basename "$target").dot"

generate_structure() {
  local dir="$1" parentName="$2" depth="$3"
  (( depth > MAX_DEPTH )) && return
  local entry
  for entry in "$dir"/*(N) "$dir"/.*(N); do
    local base="${entry:t}"
    [[ "$base" == "." || "$base" == ".." ]] && continue
    (( ${EXCLUDE_NAMES[(Ie)$base]} )) && continue
    if [[ -d "$entry" ]]; then
      local subdirName="${entry:A}"
      subdirName="${subdirName#$target/}"
      print -u2 "  ${subdirName}/"
      print "  \"$subdirName\" [label=\"${base}\"];"
      [[ -n "$parentName" ]] && print "  \"$parentName\" -> \"$subdirName\";"
      generate_structure "$entry" "$subdirName" $((depth + 1))
    elif [[ -f "$entry" ]]; then
      local fileName="${entry:A}"
      fileName="${fileName#$target/}"
      print "  \"$fileName\" [shape=box, label=\"${base}\"];"
      print "  \"$parentName\" -> \"$fileName\" [style=dotted];"
    fi
  done
}

{
  print "digraph G {"
  print "  bgcolor=\"${SYN_BG}\";"
  print "  rankdir=\"TB\";"
  print "  nodesep=1;"
  print "  ranksep=2;"
  print "  node [style=filled, fillcolor=\"${SYN_BG}\", color=\"${SYN_ACCENT}\", fontcolor=\"${SYN_TEXT}\"];"
  print "  edge [color=\"${SYN_ACCENT}\"];"
  generate_structure "$target" "" 0
  print "}"
} > "$dotFile"

print "Graphviz dot file generated: $dotFile"
print "Generating dot output..."
dot -Tpng "$dotFile" -o "$outDir/$(basename "$target")-dot.png"

print "Done. Output in $outDir"
