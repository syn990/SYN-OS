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
# stdout. Centered card, not rofi's default thin top-anchored bar: solid
# SYN_PANEL fill, thick SYN_ACCENT border, fixed width so short prompts
# (a single password field) still read as a real popup, not a sliver.
syn_pick::rofi() {
  local prompt="$1"; shift
  local bg="${SYN_BG:-#000000}" panel="${SYN_PANEL:-#2c0101}"
  local text="${SYN_TEXT:-#ffffff}" accent="${SYN_ACCENT:-#800000}"
  local accent_dim="${SYN_ACCENT_DIM:-#260101}"
  rofi -dmenu -theme-str \
    "* { background: ${bg}e6; background-color: ${panel}; foreground: $text; lightbg: $accent_dim; lightfg: $text; selected-normal-background: $accent; selected-normal-foreground: $text; border-color: $accent; } \
     window { location: center; width: 480px; border: 3px; border-radius: 0px; padding: 16px; } \
     entry { placeholder-color: $text; } \
     inputbar { border: 0 0 2px 0; border-color: $accent; padding: 4px 0; margin: 0 0 8px 0; }" \
    -p "$prompt" "$@"
}

# syn_pick::rofi_input <prompt> [default] — a real popup text field, not a
# terminal read. [default], if given, is offered as the one selectable/
# editable line — Enter accepts it as-is, or the user types over it.
# Returns empty (not an error) if the popup is dismissed with no input.
syn_pick::rofi_input() {
  printf '%s' "${2:-}" | syn_pick::rofi "$1"
}

# syn_pick::rofi_password <prompt> — same, masked with *.
syn_pick::rofi_password() {
  printf '' | syn_pick::rofi "$1" -password
}
