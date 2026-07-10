#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - D O C S - V I E W
#
#   Renders one markdown doc with glow in this terminal, and opens any
#   Graphviz diagrams it references (![...](./diagrams/svg/foo.svg)) as real image
#   windows via feh rather than terminal graphics. Invoked by
#   syn-pipe-docs.zsh, one file per call.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-DOCS-VIEW (Docs)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

file="${1:-}"
if [[ -z "$file" || ! -f "$file" ]]; then
  print -u2 "Usage: syn-docs-view.zsh <file.md>"
  exit 1
fi

docDir="${file:h}"

glow "$file"

# Paths resolve relative to the doc's own directory, same as GitHub's
# markdown rendering. `|| true` covers grep's exit 1 on no diagrams found.
svgRefs=("${(@f)$(grep -oE '!\[[^]]*\]\([^)]+\.svg\)' "$file" | sed -E 's/.*\(([^)]+)\)/\1/' || true)}")

for ref in "${svgRefs[@]}"; do
  [[ -z "$ref" ]] && continue
  svgPath="${docDir}/${ref}"
  [[ -f "$svgPath" ]] && feh "$svgPath" &
done

print
print -r -- "-- Press any key to close --"
read -k -s -u0 < /dev/tty
