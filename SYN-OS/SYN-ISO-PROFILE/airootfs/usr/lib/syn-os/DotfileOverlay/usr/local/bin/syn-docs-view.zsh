#!/usr/bin/env zsh
# SYN-OS Docs Viewer: renders one markdown doc with glow in this terminal,
# and opens any Graphviz diagrams it references (![...](./svg/foo.svg)) as
# real image windows via feh — chafa's terminal-graphics rendering was
# small and low quality regardless of scaling flags; a real image viewer
# just shows the SVG properly instead of fighting terminal-graphics
# protocols. Invoked by syn-docs-menu.zsh, one file per call.
set -euo pipefail

file="${1:-}"
if [[ -z "$file" || ! -f "$file" ]]; then
  print -u2 "Usage: syn-docs-view.zsh <file.md>"
  exit 1
fi

docDir="${file:h}"

glow "$file"

# Pull every ![...](path.svg) reference out of the doc and open each in feh,
# resolved relative to the doc's own directory (matches how the markdown
# itself resolves them when rendered on GitHub). grep exits 1 on no-match
# (most docs have zero diagrams) — `|| true` keeps that from tripping
# `set -e`. Backgrounded so feh doesn't block the terminal from closing.
svgRefs=("${(@f)$(grep -oE '!\[[^]]*\]\([^)]+\.svg\)' "$file" | sed -E 's/.*\(([^)]+)\)/\1/' || true)}")

for ref in "${svgRefs[@]}"; do
  [[ -z "$ref" ]] && continue
  svgPath="${docDir}/${ref}"
  [[ -f "$svgPath" ]] && feh "$svgPath" &
done

print
print -r -- "-- Press any key to close --"
read -k -s -u0 < /dev/tty
