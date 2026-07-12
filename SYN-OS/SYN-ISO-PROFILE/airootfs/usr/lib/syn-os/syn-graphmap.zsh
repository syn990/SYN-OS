#!/usr/bin/env zsh
# SYN-GRAPHMAP: recursively graphs a directory tree with Graphviz, in all
# five layout engines (dot/neato/twopi/circo/fdp), styled in the project's
# red/black palette. Rewrite of the original GRAPH.sh (kept for reference at
# docs/graphviz-src/GRAPH.sh.orig) — same output, run against any directory
# instead of hardcoded to one.
#
# Usage: syn-graphmap.zsh [directory]   (defaults to $PWD)

set -euo pipefail

target="${1:-$PWD}"
[[ -d "$target" ]] || { print -u2 "Not a directory: $target"; exit 1; }
target="${target:A}"

outDir="$HOME/Pictures/SYN-GRAPHMAP"
mkdir -p "$outDir"
dotFile="$outDir/$(basename "$target").dot"

generate_structure() {
  local dir="$1" parentName="$2"
  local entry
  for entry in "$dir"/*(N) "$dir"/.*(N); do
    local base="${entry:t}"
    [[ "$base" == "." || "$base" == ".." ]] && continue
    if [[ -d "$entry" ]]; then
      local subdirName="${entry:A}"
      subdirName="${subdirName#$target/}"
      print "  \"$subdirName\" [label=\"${base}\"];"
      [[ -n "$parentName" ]] && print "  \"$parentName\" -> \"$subdirName\";"
      generate_structure "$entry" "$subdirName"
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
  generate_structure "$target" ""
  print "}"
} > "$dotFile"

print "Graphviz dot file generated: $dotFile"

for layout in dot neato twopi circo fdp; do
  print "Generating ${layout} output..."
  "$layout" -Tpng "$dotFile" -o "$outDir/$(basename "$target")-${layout}.png"
done

print "Done. Output in $outDir"
