#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - G R A P H M A P
#
#   Recursively graphs a directory tree with Graphviz's dot layout, styled
#   in the live SYN-OS theme. Rewrite of the original GRAPH.sh (kept for
#   reference at docs/diagrams/src/GRAPH.sh.orig).
#
#   Usage: syn-graphmap.zsh [directory] [max-depth] [format]
#     directory   prompts via wmenu if omitted
#     max-depth   defaults to 6 (see SYN-GRAPH's Quick/Full/Custom menu)
#     format      png or svg, prompts via wmenu if omitted. PNG is raster
#                 (sharp at 100%, blurs if a viewer scales it); SVG is
#                 vector and stays sharp at any zoom.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-GRAPHMAP (Tools)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

EXCLUDE_NAMES=(.git .cache node_modules WORKDIR .syncache)
MAX_DEPTH="${2:-6}"

source /usr/lib/syn-os/syn-theme-lib.zsh
source /usr/lib/syn-os/syn-ui.zsh
source /usr/lib/syn-os/syn-picker-lib.zsh
syn_theme_load

target="${1:-}"
if [[ -z "$target" ]]; then
  target="$(print -l -- "$PWD" "$HOME" "$HOME/GithubProjects" \
    | syn_pick::rofi "Directory to graph:")"
fi
[[ -n "$target" ]] || { syn_ui::error "No directory given."; exit 1; }
[[ -d "$target" ]] || { syn_ui::error "Not a directory: $target"; exit 1; }
target="${target:A}"

format="${3:-}"
if [[ -z "$format" ]]; then
  format="$(print -l -- png svg | syn_pick::rofi "Output format:")"
fi
case "$format" in
  png|svg) ;;
  *) syn_ui::error "Unknown format '${format}', defaulting to png."; format="png" ;;
esac

syn_ui::step "Scanning ${target}"

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

syn_ui::step_done "Graphviz dot file generated: $dotFile"
syn_ui::step "Generating ${format} output"
# -Gdpi only affects rendering resolution, not layout, so it's PNG-only —
# Graphviz's default 72 DPI is too coarse to read labels once zoomed in.
typeset -a dpiFlag
[[ "$format" == "png" ]] && dpiFlag=(-Gdpi=300)
dot "${dpiFlag[@]}" -T"${format}" "$dotFile" -o "$outDir/$(basename "$target")-dot.${format}"

syn_ui::step_done "Done. Output in $outDir"
