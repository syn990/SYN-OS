#!/usr/bin/env zsh
# ------------------------------------------------------------------------------
#                         S Y N - P I C K E R - L I B
#
#   Themed dmenu-style pickers shared by capture tools (screenshot.zsh,
#   screen-recorder.zsh) and anything else that needs a "pick one of
#   these lines" prompt. Two backends, syn_pick::wmenu and syn_pick::rofi,
#   both themed from syn-theme-lib.zsh's live SYN_* palette and callable
#   the same way (choices on stdin, prompt as $1, chosen line on stdout) —
#   scripts can call either directly today; a later preference toggle can
#   pick between them without either backend changing shape.
#
#   Zsh only (uses zsh-only array syntax) — unlike syn-theme-lib.zsh, this
#   is never sourced from labwc's /bin/sh autostart.
#
#   SYN-OS     : The Syntax Operating System
#   Component  : SYN-PICKER-LIB (Desktop)
#   Author     : William Hayward-Holland (Syntax990)
#   License    : MIT License
# ------------------------------------------------------------------------------

# syn_pick::wmenu <prompt> — choices on stdin, one per line; chosen line on
# stdout. -l 5: vertical list of up to 5 presets visible at once, not a
# single-line bar easy to miss. Selection highlight uses the theme's
# full-strength SYN_ACCENT (not the dim/muted variant) so the picked row
# stands out.
syn_pick::wmenu() {
  local prompt="$1"
  local bg="${SYN_BG:-#000000}" text="${SYN_TEXT:-#ffffff}"
  local accent="${SYN_ACCENT:-#800000}" accent_dim="${SYN_ACCENT_DIM:-#260101}"
  wmenu -l 5 -f "Terminus 14" \
    -N "$bg" -n "$text" -S "$accent" -s "$bg" -M "$accent_dim" -m "$text" \
    -p "$prompt"
}

# syn_pick::rofi <prompt> [rofi-args...] — choices on stdin, chosen line on
# stdout. Full root-level theme override (`* { ... }`) — a partial one
# leaves rofi on its stock light theme.
syn_pick::rofi() {
  local prompt="$1"; shift
  local bg="${SYN_BG:-#000000}" bg_alt="${SYN_BG_ALT:-#100000}"
  local text="${SYN_TEXT:-#ffffff}" accent="${SYN_ACCENT:-#800000}"
  local border="${SYN_BORDER:-#444444}"
  rofi -dmenu -theme-str \
    "* { background: ${bg}e6; background-color: ${bg}e6; foreground: $text; lightbg: $bg_alt; lightfg: $text; selected-normal-background: $accent; selected-normal-foreground: $text; border-color: $border; } window { border: 1px; }" \
    -p "$prompt" "$@"
}
