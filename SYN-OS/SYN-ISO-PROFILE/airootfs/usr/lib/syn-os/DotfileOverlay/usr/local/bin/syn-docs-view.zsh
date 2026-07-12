#!/usr/bin/env zsh
# SYN-OS Docs Viewer: renders one markdown doc with glow, then inlines any
# Graphviz diagrams it references (![...](./svg/foo.svg)) with chafa.
# Invoked by syn-docs-menu.zsh, one file per call.
set -euo pipefail

file="${1:-}"
if [[ -z "$file" || ! -f "$file" ]]; then
  print -u2 "Usage: syn-docs-view.zsh <file.md>"
  exit 1
fi

docDir="${file:h}"

glow "$file"

# Pull every ![...](path.svg) reference out of the doc and render each
# inline, resolved relative to the doc's own directory (matches how the
# markdown itself resolves them when rendered on GitHub). grep exits 1 on
# no-match (most docs have zero diagrams), which would otherwise trip
# `set -e` and kill this script before the "press any key" prompt below —
# the `|| true` is load-bearing, not decorative.
svgRefs=("${(@f)$(grep -oE '!\[[^]]*\]\([^)]+\.svg\)' "$file" | sed -E 's/.*\(([^)]+)\)/\1/' || true)}")

for ref in "${svgRefs[@]}"; do
  [[ -z "$ref" ]] && continue
  svgPath="${docDir}/${ref}"
  if [[ -f "$svgPath" ]]; then
    print
    print "-- ${ref:t} --"
    chafa "$svgPath"
  fi
done

print
print "-- Press any key to close --"
read -k1 -s
