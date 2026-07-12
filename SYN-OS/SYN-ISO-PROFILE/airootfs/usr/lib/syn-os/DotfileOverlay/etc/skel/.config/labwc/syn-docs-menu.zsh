#!/usr/bin/env zsh
# SYN-OS Docs Menu
# Labwc pipe menu listing /usr/share/syn-os/docs/*.md (and concepts/*.md),
# each entry opens syn-docs-view.zsh against that file. See docs/labwc.md.
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
  # syn-docs-view.zsh is interactive (renders text, waits on a keypress) so
  # it needs a held-open terminal, unlike syn-theme-apply above which runs
  # instantly and exits — same reasoning as SYN-CRYPTER/SYN-REDSHIRT's
  # foot wrapping in menu.xml. Absolute path to the script itself: `foot -e`
  # execs argv directly (no shell), so it never searches $PATH the way a
  # login shell would — an unqualified `syn-docs-view.zsh` here silently
  # fails to exec and foot just closes, no visible error anywhere.
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
