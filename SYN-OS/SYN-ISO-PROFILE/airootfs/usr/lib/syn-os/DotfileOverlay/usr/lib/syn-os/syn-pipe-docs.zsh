#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P I P E - D O C S
#
#   Generates a labwc pipe menu listing /usr/share/syn-os/docs/*.md (and
#   concepts/*.md), each entry opening syn-docs-view.zsh against that
#   file. See docs/labwc.md.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PIPE-DOCS (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------
set -euo pipefail

DOCS_DIR="/usr/share/syn-os/docs"

print '<?xml version="1.0" encoding="UTF-8"?>'
print '<openbox_pipe_menu>'

xml_escape() {
  print -r -- "$1" | sed \
    -e 's/&/\&amp;/g' \
    -e 's/"/\&quot;/g' \
    -e "s/'/\&apos;/g" \
    -e 's/</\&lt;/g' \
    -e 's/>/\&gt;/g'
}

# label from filename: "installer-overview.md" -> "Installer Overview"
title_for() {
  local base="${1:t:r}"
  print -r -- "${(C)${base//-/ }}"
}

emit_entry() {
  local f="$1" label
  label="$(xml_escape "$(title_for "$f")")"
  print "  <item label=\"$label\">"
  # foot -e execs argv directly, no shell, so it never searches $PATH —
  # the absolute path is required or foot just closes with no error.
  print "    <action name=\"Execute\"><command>foot -e /usr/local/bin/syn-docs-view.zsh ${f}</command></action>"
  print "  </item>"
}

if [[ ! -d "$DOCS_DIR" ]] || ! ls "$DOCS_DIR"/*.md >/dev/null 2>&1; then
  print '<item label="No docs found under /usr/share/syn-os/docs"/>'
  print '</openbox_pipe_menu>'
  exit 0
fi

for f in "$DOCS_DIR"/*.md(N); do
  emit_entry "$f"
done

if ls "$DOCS_DIR"/concepts/*.md >/dev/null 2>&1; then
  print '  <separator label="CONCEPTS"/>'
  for f in "$DOCS_DIR"/concepts/*.md(N); do
    emit_entry "$f"
  done
fi

print '</openbox_pipe_menu>'
