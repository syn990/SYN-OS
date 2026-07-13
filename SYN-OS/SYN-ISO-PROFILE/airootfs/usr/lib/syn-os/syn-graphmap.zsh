#!/usr/bin/env zsh
# SYN-GRAPHMAP: recursively graphs a directory tree with Graphviz's dot
# layout, styled in the project's red/black palette. Rewrite of the
# original GRAPH.sh (kept for reference at docs/graphviz-src/GRAPH.sh.orig).
#
# Usage: syn-graphmap.zsh [directory]   (prompts via wmenu if omitted)

set -euo pipefail

EXCLUDE_NAMES=(.git .cache node_modules WORKDIR .syncache)
MAX_DEPTH=6

target="${1:-}"
if [[ -z "$target" ]]; then
  print "SYN-GRAPHMAP: a popup will ask which directory to graph — enter or pick one, then check back here."
  target="$(print -l -- "$PWD" "$HOME" "$HOME/GithubProjects" \
    | wmenu -p "Directory to graph:")"
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
  print "  bgcolor=\"#000000\";"
  print "  rankdir=\"TB\";"
  print "  nodesep=1;"
  print "  ranksep=2;"
  print "  node [style=filled, fillcolor=\"#000000\", color=\"#8B0000\", fontcolor=\"#FF0000\"];"
  print "  edge [color=\"#FF0000\"];"
  generate_structure "$target" "" 0
  print "}"
} > "$dotFile"

print "Graphviz dot file generated: $dotFile"
print "Generating dot output..."
dot -Tpng "$dotFile" -o "$outDir/$(basename "$target")-dot.png"

print "Done. Output in $outDir"
